#include "rx/semantic.hpp"
#include "rx/semantic_literal.hpp"

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <functional>
#include <iterator>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <sstream>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

namespace rx {
namespace {

using Ty = SemanticType;
using TyPtr = SemanticTypePtr;

TyPtr ty(Ty::Kind kind) {
  auto value = std::make_shared<Ty>();
  value->kind = kind;
  return value;
}

TyPtr compound(Ty::Kind kind, TyPtr element, bool is_mutable = false) {
  auto value = ty(kind);
  value->element = std::move(element);
  value->is_mutable = is_mutable;
  return value;
}

bool same(const TyPtr &lhs, const TyPtr &rhs) {
  // Error types compare successfully to suppress follow-on diagnostics.
  if (!lhs || !rhs)
    return false;
  if (lhs->kind == Ty::Kind::Error || rhs->kind == Ty::Kind::Error)
    return true;
  if (lhs->kind != rhs->kind)
    return false;
  if (lhs->kind == Ty::Kind::Struct || lhs->kind == Ty::Kind::Function)
    return lhs->name == rhs->name;
  if (lhs->kind == Ty::Kind::Array)
    return lhs->length == rhs->length && same(lhs->element, rhs->element);
  if (lhs->kind == Ty::Kind::Box || lhs->kind == Ty::Kind::Vec)
    return same(lhs->element, rhs->element);
  if (lhs->kind == Ty::Kind::Ref)
    return lhs->is_mutable == rhs->is_mutable &&
           same(lhs->element, rhs->element);
  return true;
}

bool integer(const TyPtr &value) {
  return value &&
         (value->kind == Ty::Kind::I32 || value->kind == Ty::Kind::U32 ||
          value->kind == Ty::Kind::Isize || value->kind == Ty::Kind::Usize);
}

bool signed_integer(const TyPtr &value) {
  return value &&
         (value->kind == Ty::Kind::I32 || value->kind == Ty::Kind::Isize);
}

std::string type_name(const TyPtr &value) {
  if (!value)
    return "<missing>";
  switch (value->kind) {
  case Ty::Kind::Error:
    return "<error>";
  case Ty::Kind::Never:
    return "!";
  case Ty::Kind::Unit:
    return "()";
  case Ty::Kind::Bool:
    return "bool";
  case Ty::Kind::I32:
    return "i32";
  case Ty::Kind::U32:
    return "u32";
  case Ty::Kind::Isize:
    return "isize";
  case Ty::Kind::Usize:
    return "usize";
  case Ty::Kind::Struct:
    return value->name;
  case Ty::Kind::Box:
    return "Box<" + type_name(value->element) + ">";
  case Ty::Kind::Vec:
    return "Vec<" + type_name(value->element) + ">";
  case Ty::Kind::Array:
    return "[" + type_name(value->element) + "; " +
           std::to_string(value->length) + "]";
  case Ty::Kind::Ref:
    return std::string("&") + (value->is_mutable ? "mut " : "") +
           type_name(value->element);
  case Ty::Kind::Function:
    return "fn " + value->name;
  }
  return "<?>";
}

struct ConstantInfo {
  const ast::Constant *ast{};
  TyPtr type;
  std::string owner;
  enum class TypeState {
    Fresh,
    Resolving,
    Done,
    Failed
  } type_state{TypeState::Fresh};
  enum class State { Fresh, Evaluating, Done, Failed } state{State::Fresh};
  std::int64_t value{};
};

struct StructInfo {
  const ast::Struct *ast{};
  std::unordered_map<std::string, TyPtr> fields;
  std::unordered_map<std::string, SymbolId> field_symbols;
  std::unordered_set<std::string> derives;
  enum class LayoutState {
    Fresh,
    Visiting,
    Valid,
    Invalid
  } layout{LayoutState::Fresh};
};

struct FunctionInfo {
  const ast::Function *ast{};
  std::vector<TyPtr> parameters;
  TyPtr result;
  std::string owner;
};

struct ExprInfo {
  ExprInfo(TyPtr type = ty(Ty::Kind::Error), bool is_place = false,
           bool is_mutable = false, bool access_locked = false,
           std::optional<SymbolId> symbol = {}, std::string target = {},
           std::vector<Adjustment> adjustments = {})
      : type(std::move(type)), is_place(is_place), is_mutable(is_mutable),
        access_locked(access_locked), symbol(symbol), target(std::move(target)),
        adjustments(std::move(adjustments)) {}

  TyPtr type;
  bool is_place{};
  bool is_mutable{};
  bool access_locked{};
  std::optional<SymbolId> symbol;
  std::string target;
  std::vector<Adjustment> adjustments;
};

struct Binding {
  TyPtr type;
  bool is_mutable{};
  SymbolId symbol{};
};

class Analyzer {
public:
  Analyzer(const ast::Crate &crate, DiagnosticEngine &diagnostics)
      : crate_(crate), diagnostics_(diagnostics) {}

  SemanticResult run() {
    result_.source_crate = &crate_;
    // Each pass consumes complete tables produced by the earlier passes.
    collect_items();
    resolve_declarations();
    validate_constants();
    validate_layouts_and_derives();
    validate_entry();
    check_functions();
    result_.summary = {functions_.size(), structures_.size(),
                       constants_.size()};
    std::sort(result_.symbols.begin(), result_.symbols.end(),
              [](const SymbolRecord &left, const SymbolRecord &right) {
                return left.id < right.id;
              });
    for (auto &[_, fact] : expression_facts_)
      result_.expressions.push_back(std::move(fact));
    std::sort(result_.functions.begin(), result_.functions.end(),
              [](const auto &a, const auto &b) { return a.symbol < b.symbol; });
    std::sort(result_.structures.begin(), result_.structures.end(),
              [](const auto &a, const auto &b) { return a.symbol < b.symbol; });
    for (auto &[node, type] : resolved_types_)
      result_.resolved_types.push_back({node, std::move(type)});
    for (auto &[symbol, type] : symbol_types_)
      result_.symbol_types.push_back({symbol, std::move(type)});
    for (const auto &[_, constant] : constants_)
      if (constant.state == ConstantInfo::State::Done)
        result_.constants.push_back(
            {symbol_for(constant.ast->id),
             static_cast<std::uint32_t>(constant.value)});
    for (const auto &[_, constant] : associated_constants_)
      if (constant.state == ConstantInfo::State::Done)
        result_.constants.push_back(
            {symbol_for(constant.ast->id),
             static_cast<std::uint32_t>(constant.value)});
    std::sort(result_.constants.begin(), result_.constants.end(),
              [](const auto &a, const auto &b) { return a.symbol < b.symbol; });
    result_.success = !diagnostics_.has_errors();
    return std::move(result_);
  }

private:
  void report(Span span, std::string message) {
    diagnostics_.error(span, std::move(message));
  }

  SymbolId symbol_for(ast::NodeId node) {
    auto [entry, inserted] =
        declaration_symbols_.try_emplace(node, next_symbol_);
    if (inserted)
      ++next_symbol_.value;
    return entry->second;
  }
  void record_symbol(ast::NodeId node, SymbolKind kind, std::string name,
                     std::optional<ast::NodeId> owner = {}) {
    auto id = symbol_for(node);
    std::optional<SymbolId> parent;
    if (owner)
      parent = symbol_for(*owner);
    if (recorded_symbols_.insert(id).second)
      result_.symbols.push_back({id, kind, std::move(name), parent, node});
  }
  void record_symbol_type(ast::NodeId declaration, const TyPtr &type) {
    symbol_types_[symbol_for(declaration)] = type;
  }

  void collect_items() {
    std::unordered_set<std::string> type_names;
    std::unordered_set<std::string> value_names;
    for (const auto &item : crate_.items) {
      ast::visit(item, [&](const ast::Node &, const auto &value) {
        using T = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<T, ast::Struct>) {
          record_symbol(value.id, SymbolKind::Struct, value.name.text);
          if (builtin_type(value.name.text))
            report(value.span, "the builtin type name '" + value.name.text +
                                   "' is protected");
          if (!type_names.insert(value.name.text).second)
            report(value.span, "duplicate type name '" + value.name.text + "'");
          structures_[value.name.text].ast = &value;
        } else if constexpr (std::is_same_v<T, ast::Function>) {
          record_symbol(value.id, SymbolKind::Function, value.name.text);
          if (protected_builtin_value(value.name.text))
            report(value.span, "the builtin value name '" + value.name.text +
                                   "' is protected");
          if (!value_names.insert(value.name.text).second)
            report(value.span,
                   "duplicate value name '" + value.name.text + "'");
          functions_[value.name.text].ast = &value;
        } else if constexpr (std::is_same_v<T, ast::Constant>) {
          record_symbol(value.id, SymbolKind::Constant, value.name.text);
          if (protected_builtin_value(value.name.text))
            report(value.span, "the builtin value name '" + value.name.text +
                                   "' is protected");
          if (!value_names.insert(value.name.text).second)
            report(value.span,
                   "duplicate value name '" + value.name.text + "'");
          constants_[value.name.text].ast = &value;
        } else if constexpr (std::is_same_v<T, ast::Impl>) {
          impls_.push_back(&value);
        } else {
          static_assert(std::is_same_v<T, ast::Use>,
                        "unhandled AST declaration");
          (void)value;
        }
      });
    }
  }

  static bool builtin_type(std::string_view name) {
    return name == "bool" || name == "i32" || name == "u32" ||
           name == "isize" || name == "usize" || name == "Box" ||
           name == "Vec" || name == "Copy" || name == "Clone" ||
           name == "PartialEq" || name == "Eq";
  }

  static bool protected_builtin_value(std::string_view name) {
    return name == "getInt" || name == "printInt" || name == "printlnInt";
  }

  TyPtr resolve_type(const ast::TypePtr &syntax,
                     std::string_view self_name = {}) {
    auto result = resolve_type_impl(syntax, self_name);
    resolved_types_[syntax->id] = result;
    return result;
  }

  static std::vector<const ast::TypePtr *>
  type_arguments(const std::optional<ast::GenericArguments> &arguments) {
    std::vector<const ast::TypePtr *> result;
    if (arguments)
      for (const auto &argument : arguments->values)
        if (auto *type = std::get_if<ast::TypePtr>(&argument))
          result.push_back(type);
    return result;
  }
  static std::vector<const ast::TypePtr *>
  type_arguments(const ast::Path &path) {
    std::vector<const ast::TypePtr *> result;
    for (const auto &segment : path.segments) {
      auto arguments = type_arguments(segment.arguments);
      result.insert(result.end(), arguments.begin(), arguments.end());
    }
    return result;
  }
  TyPtr resolve_type(const std::optional<ast::TypePtr> &syntax,
                     std::string_view self_name = {}) {
    return syntax ? resolve_type(*syntax, self_name) : ty(Ty::Kind::Unit);
  }
  TyPtr resolve_type_impl(const ast::TypePtr &syntax,
                          std::string_view self_name) {
    return ast::visit(
        *syntax,
        ast::Overloaded{
            [&](const ast::TypeSyntax &, const ast::UnitType &) {
              return ty(Ty::Kind::Unit);
            },
            [&](const ast::TypeSyntax &, const ast::ParenType &p) {
              return resolve_type(p.inner, self_name);
            },
            [&](const ast::TypeSyntax &, const ast::ReferenceType &p) {
              return compound(Ty::Kind::Ref,
                              resolve_type(p.referent, self_name),
                              p.mutability == ast::Mutability::Mutable);
            },
            [&](const ast::TypeSyntax &, const ast::ArrayType &p) {
              auto value =
                  compound(Ty::Kind::Array, resolve_type(p.element, self_name));
              if (auto length = evaluate_length(p.length, self_name))
                value->length = *length;
              return value;
            },
            [&](const ast::TypeSyntax &node, const ast::PathType &p) {
              return resolve_path_type(p.path, node.span, self_name);
            }});
  }
  TyPtr resolve_path_type(const ast::Path &path, Span span,
                          std::string_view self_name) {
    const std::string name{ast::name_text(path.segments.back().name)};
    auto arguments = type_arguments(path);
    if (name == "Self") {
      if (self_name.empty()) {
        report(span, "Self is only available inside a struct or impl");
        return ty(Ty::Kind::Error);
      }
      auto value = ty(Ty::Kind::Struct);
      value->name = self_name;
      return value;
    }
    static const std::unordered_map<std::string, Ty::Kind> primitives = {
        {"bool", Ty::Kind::Bool},
        {"i32", Ty::Kind::I32},
        {"u32", Ty::Kind::U32},
        {"isize", Ty::Kind::Isize},
        {"usize", Ty::Kind::Usize}};
    if (auto it = primitives.find(name); it != primitives.end()) {
      if (!arguments.empty())
        report(span, "primitive type '" + name + "' takes no type arguments");
      return ty(it->second);
    }
    if (name == "Box" || name == "Vec") {
      if (arguments.size() != 1) {
        report(span, name + " requires exactly one concrete type argument");
        return ty(Ty::Kind::Error);
      }
      return compound(name == "Box" ? Ty::Kind::Box : Ty::Kind::Vec,
                      resolve_type(*arguments.front(), self_name));
    }
    if (structures_.contains(name)) {
      if (!arguments.empty())
        report(span, "struct '" + name + "' has no type parameters");
      auto value = ty(Ty::Kind::Struct);
      value->name = name;
      return value;
    }
    report(span, "unknown type '" + name + "'");
    return ty(Ty::Kind::Error);
  }
  std::optional<std::uint64_t>
  evaluate_length(const ast::ExprPtr &expression,
                  std::string_view self_name = {}) {
    auto errors = diagnostics_.error_count();
    auto [type, value] =
        constant_value(expression, ty(Ty::Kind::Usize), self_name);
    if (type->kind != Ty::Kind::Usize)
      report(expression->span, "array length must have type usize");
    if (value < 0)
      report(expression->span, "array length cannot be negative");
    if (diagnostics_.error_count() != errors)
      return {};
    return static_cast<std::uint64_t>(value);
  }

  void resolve_constant_type(ConstantInfo &constant) {
    if (constant.type_state == ConstantInfo::TypeState::Done ||
        constant.type_state == ConstantInfo::TypeState::Failed)
      return;
    if (constant.type_state == ConstantInfo::TypeState::Resolving) {
      report(constant.ast->span, "constant type dependency cycle");
      constant.type = ty(Ty::Kind::Error);
      constant.type_state = ConstantInfo::TypeState::Failed;
      return;
    }
    constant.type_state = ConstantInfo::TypeState::Resolving;
    constant.type = resolve_type(constant.ast->type, constant.owner);
    record_symbol_type(constant.ast->id, constant.type);
    if (constant.type_state != ConstantInfo::TypeState::Failed)
      constant.type_state = ConstantInfo::TypeState::Done;
  }

  void resolve_declarations() {
    // Register every associated item before resolving declaration types.
    for (const auto *implementation : impls_) {
      auto target = resolve_type(implementation->target);
      if (target->kind != Ty::Kind::Struct ||
          !structures_.contains(target->name)) {
        report(implementation->span,
               "an inherent impl target must be a user-defined struct");
        continue;
      }
      const std::string owner = target->name;
      const auto owner_symbol = structures_.at(owner).ast->id;
      std::unordered_set<std::string> associated_names;
      for (const auto &member : implementation->members) {
        ast::visit(member, [&](const ast::Node &, const auto &declaration) {
          using T = std::decay_t<decltype(declaration)>;
          const auto &name = declaration.name.text;
          auto key = owner + "::" + name;
          record_symbol(declaration.id,
                        std::is_same_v<T, ast::Function>
                            ? SymbolKind::AssociatedFunction
                            : SymbolKind::AssociatedConstant,
                        key, owner_symbol);
          if (!associated_names.insert(name).second ||
              associated_functions_.contains(key) ||
              associated_constants_.contains(key))
            report(declaration.span,
                   "duplicate associated item '" + name + "'");
          if constexpr (std::is_same_v<T, ast::Function>) {
            associated_functions_[key].ast = &declaration;
            associated_functions_[key].owner = owner;
          } else {
            static_assert(std::is_same_v<T, ast::Constant>);
            associated_constants_[key].ast = &declaration;
            associated_constants_[key].owner = owner;
          }
        });
      }
    }

    // Constant types resolve lazily so their order cannot affect array lengths.
    for (auto &[_, constant] : constants_)
      resolve_constant_type(constant);
    for (auto &[_, constant] : associated_constants_)
      resolve_constant_type(constant);
    for (auto &[name, function] : associated_functions_)
      resolve_function(function, name, function.owner);
    for (auto &[name, structure] : structures_) {
      std::unordered_set<std::string> fields;
      std::unordered_set<std::string> derives;
      for (const auto &attribute : structure.ast->attributes)
        for (const auto &name : attribute.names) {
          const std::string derive{ast::spelling(name.kind)};
          if (!derives.insert(derive).second)
            report(structure.ast->span, "duplicate derive '" + derive + "'");
          structure.derives.insert(derive);
        }
      if (structure.derives.contains("Copy") &&
          !structure.derives.contains("Clone"))
        report(structure.ast->span, "Copy requires an explicit Clone derive");
      if (structure.derives.contains("Eq") &&
          !structure.derives.contains("PartialEq"))
        report(structure.ast->span, "Eq requires an explicit PartialEq derive");
      for (const auto &field : structure.ast->fields) {
        record_symbol(field.id, SymbolKind::Field, field.name.text,
                      structure.ast->id);
        if (!fields.insert(field.name.text).second)
          report(field.span, "duplicate field '" + field.name.text + "'");
        structure.fields[field.name.text] = resolve_type(field.type, name);
        record_symbol_type(field.id, structure.fields[field.name.text]);
        structure.field_symbols[field.name.text] = symbol_for(field.id);
      }
      StructSemantics declaration{symbol_for(structure.ast->id), {}};
      for (const auto &field : structure.ast->fields)
        declaration.fields.push_back(symbol_for(field.id));
      result_.structures.push_back(std::move(declaration));
    }
    for (auto &[name, function] : functions_)
      resolve_function(function, name, {});
  }

  void resolve_function(FunctionInfo &function, const std::string &,
                        std::string owner) {
    function.owner = std::move(owner);
    std::unordered_set<std::string> parameters;
    for (const auto &parameter : function.ast->signature.parameters) {
      if (!parameters.insert(parameter.binding.name.text).second)
        report(parameter.span,
               "duplicate parameter '" + parameter.binding.name.text + "'");
      function.parameters.push_back(
          resolve_type(parameter.type, function.owner));
    }
    function.result =
        resolve_type(function.ast->signature.return_annotation, function.owner);
  }

  void validate_constants() {
    for (auto &[_, constant] : constants_)
      evaluate_constant(constant);
    for (auto &[_, constant] : associated_constants_)
      evaluate_constant(constant);
  }

  void evaluate_constant(ConstantInfo &constant) {
    // The visit state turns recursive evaluation into cycle detection.
    resolve_constant_type(constant);
    if (constant.state == ConstantInfo::State::Done ||
        constant.state == ConstantInfo::State::Failed)
      return;
    if (constant.state == ConstantInfo::State::Evaluating) {
      report(constant.ast->span, "constant dependency cycle");
      constant.state = ConstantInfo::State::Failed;
      return;
    }
    constant.state = ConstantInfo::State::Evaluating;
    auto [value_type, value] =
        constant_value(constant.ast->value, constant.type, constant.owner);
    if (!same(constant.type, value_type)) {
      report(constant.ast->span, "constant initializer has type " +
                                     type_name(value_type) + ", expected " +
                                     type_name(constant.type));
      constant.state = ConstantInfo::State::Failed;
      return;
    }
    constant.value = value;
    constant.state = ConstantInfo::State::Done;
  }

  using EvaluatedConstant = std::pair<TyPtr, std::int64_t>;
  EvaluatedConstant constant_path(const ast::Expr &node,
                                  const ast::PathExpr &path,
                                  std::string_view self_name) {
    auto name = ast::path_name(path.path);
    if (!self_name.empty() && name.starts_with("Self::"))
      name = std::string(self_name) + name.substr(4);
    if (auto it = constants_.find(name); it != constants_.end()) {
      evaluate_constant(it->second);
      record_node(node, ExprInfo{it->second.type, false, false, false,
                                 symbol_for(it->second.ast->id), name});
      return {it->second.type, it->second.value};
    }
    if (auto it = associated_constants_.find(name);
        it != associated_constants_.end()) {
      evaluate_constant(it->second);
      record_node(node, ExprInfo{it->second.type, false, false, false,
                                 symbol_for(it->second.ast->id), name});
      return {it->second.type, it->second.value};
    }
    report(node.span, "constant path does not resolve to a constant");
    auto error = ty(Ty::Kind::Error);
    record_node(node, ExprInfo{error});
    return {error, 0};
  }
  EvaluatedConstant constant_value(const ast::ExprPtr &expression,
                                   TyPtr expected = {},
                                   std::string_view self_name = {}) {
    return ast::visit(
        *expression,
        ast::Overloaded{
            [&](const ast::Expr &node,
                const ast::ParenExpr &p) -> EvaluatedConstant {
              auto value = constant_value(p.inner, expected, self_name);
              record_node(node, ExprInfo{value.first});
              return value;
            },
            [&](const ast::Expr &node,
                const ast::BooleanExpr &p) -> EvaluatedConstant {
              auto type = ty(Ty::Kind::Bool);
              record_node(node, ExprInfo{type});
              return {type, p.value};
            },
            [&](const ast::Expr &node,
                const ast::IntegerExpr &p) -> EvaluatedConstant {
              auto type = literal_type(p.literal, expected, node.span);
              auto magnitude = integer_magnitude(p.literal).value_or(0);
              // Invalid magnitudes must not overflow signed arithmetic during
              // recovery.
              if (magnitude > std::numeric_limits<std::uint32_t>::max())
                magnitude = 0;
              record_node(node, ExprInfo{type});
              return {type, static_cast<std::int64_t>(magnitude)};
            },
            [&](const ast::Expr &node,
                const ast::UnaryExpr &p) -> EvaluatedConstant {
              if (p.op != ast::UnaryOp::Negate)
                return invalid_constant(node);
              auto [type, value] =
                  constant_value(p.operand, expected, self_name);
              if (!signed_integer(type))
                report(node.span, "only signed constants may be negated");
              record_node(node, ExprInfo{type});
              return {type, -value};
            },
            [&](const ast::Expr &node,
                const ast::PathExpr &p) -> EvaluatedConstant {
              return constant_path(node, p, self_name);
            },
            [&](const ast::Expr &node, const auto &) -> EvaluatedConstant {
              return invalid_constant(node);
            }});
  }
  EvaluatedConstant invalid_constant(const ast::Expr &node) {
    report(node.span, "invalid constant expression");
    auto error = ty(Ty::Kind::Error);
    record_node(node, ExprInfo{error});
    return {error, 0};
  }

  void validate_layouts_and_derives() {
    for (auto &[name, _] : structures_)
      validate_layout(name);
    for (auto &[name, structure] : structures_) {
      for (const auto &derive : structure.derives) {
        for (const auto &[_, field] : structure.fields) {
          if (!supports(field, derive, name, {})) {
            report(structure.ast->span, "field type " + type_name(field) +
                                            " does not support derived " +
                                            derive);
            break;
          }
        }
      }
    }
  }

  bool validate_layout(const std::string &name) {
    // Layout DFS rejects inline recursion while references remain finite.
    auto &structure = structures_.at(name);
    if (structure.layout == StructInfo::LayoutState::Valid)
      return true;
    if (structure.layout == StructInfo::LayoutState::Invalid)
      return false;
    if (structure.layout == StructInfo::LayoutState::Visiting) {
      report(structure.ast->span, "recursive type has infinite inline layout");
      structure.layout = StructInfo::LayoutState::Invalid;
      return false;
    }
    structure.layout = StructInfo::LayoutState::Visiting;
    bool valid = true;
    std::function<void(const TyPtr &)> visit = [&](const TyPtr &field) {
      if (field->kind == Ty::Kind::Struct)
        valid &= validate_layout(field->name);
      else if (field->kind == Ty::Kind::Array)
        visit(field->element);
    };
    for (const auto &[_, field] : structure.fields)
      visit(field);
    structure.layout = valid ? StructInfo::LayoutState::Valid
                             : StructInfo::LayoutState::Invalid;
    return valid;
  }

  bool supports(const TyPtr &value, const std::string &trait,
                const std::string &root, std::unordered_set<std::string> seen) {
    if (!value)
      return false;
    if (value->kind == Ty::Kind::Bool || integer(value) ||
        value->kind == Ty::Kind::Unit)
      return true;
    if (value->kind == Ty::Kind::Ref) {
      if (trait == "Copy")
        return !value->is_mutable;
      if (trait == "Clone")
        return !value->is_mutable;
      if (trait == "PartialEq" || trait == "Eq")
        return supports(value->element, trait, root, seen);
    }
    if (value->kind == Ty::Kind::Box || value->kind == Ty::Kind::Vec ||
        value->kind == Ty::Kind::Array) {
      if (trait == "Copy" && value->kind != Ty::Kind::Array)
        return false;
      return supports(value->element, trait, root, std::move(seen));
    }
    if (value->kind == Ty::Kind::Struct) {
      if (seen.contains(value->name))
        return trait != "Copy";
      seen.insert(value->name);
      auto it = structures_.find(value->name);
      if (it == structures_.end() || !it->second.derives.contains(trait))
        return false;
      for (const auto &[_, field] : it->second.fields)
        if (!supports(field, trait, root, seen))
          return false;
      return true;
    }
    return false;
  }

  void validate_entry() {
    auto it = functions_.find("main");
    if (it == functions_.end()) {
      report(crate_.span, "an executable requires a top-level main function");
      return;
    }
    const auto &main = it->second;
    if (!main.parameters.empty())
      report(main.ast->span, "main cannot have value parameters");
    if (main.ast->generics && !main.ast->generics->lifetimes.empty())
      report(main.ast->span, "main cannot have generic parameters");
    if (main.result->kind != Ty::Kind::Unit)
      report(main.ast->span, "main must return unit");
  }

  void check_functions() {
    for (const auto &[_, function] : functions_)
      check_function(function);
    for (const auto &[_, function] : associated_functions_)
      check_function(function);
  }

  void check_function(const FunctionInfo &function) {
    FunctionSemantics signature{
        symbol_for(function.ast->id), {}, {}, function.result};
    // Every function starts with isolated scopes and loop context.
    scopes_.clear();
    scopes_.emplace_back();
    current_function_ = &function;
    if (function.ast->signature.receiver.has_value()) {
      if (function.owner.empty())
        report(function.ast->span, "self is only available in a method");
      auto self = ty(Ty::Kind::Struct);
      self->name = function.owner;
      if (ast::by_reference(*function.ast->signature.receiver))
        self = compound(Ty::Kind::Ref, self,
                        ast::is_mutable(*function.ast->signature.receiver));
      record_symbol(function.ast->signature.receiver->id, SymbolKind::Receiver,
                    "self", function.ast->id);
      record_symbol_type(function.ast->signature.receiver->id, self);
      signature.receiver = ParameterSemantics{
          symbol_for(function.ast->signature.receiver->id), self};
      scopes_.back()["self"] = {
          self, ast::is_mutable(*function.ast->signature.receiver),
          symbol_for(function.ast->signature.receiver->id)};
    }
    for (std::size_t i = 0; i < function.ast->signature.parameters.size();
         ++i) {
      const auto &parameter = function.ast->signature.parameters[i];
      if (protected_builtin_value(parameter.binding.name.text))
        report(parameter.span, "cannot shadow a protected builtin");
      record_symbol(parameter.id, SymbolKind::Parameter,
                    parameter.binding.name.text, function.ast->id);
      record_symbol_type(parameter.id, function.parameters[i]);
      signature.parameters.push_back(
          {symbol_for(parameter.id), function.parameters[i]});
      scopes_.back()[parameter.binding.name.text] = {
          function.parameters[i],
          parameter.binding.mutability == ast::Mutability::Mutable,
          symbol_for(parameter.id)};
    }
    auto body = check_block(*function.ast->body, function.result);
    if (!coercible(body.type, function.result))
      mismatch(function.ast->body->span, body.type, function.result);
    current_function_ = nullptr;
    result_.functions.push_back(std::move(signature));
  }

  Binding *lookup(const std::string &name) {
    for (auto it = scopes_.rbegin(); it != scopes_.rend(); ++it)
      if (auto found = it->find(name); found != it->end())
        return &found->second;
    return nullptr;
  }

  bool coercible(const TyPtr &from, const TyPtr &to) {
    if (!from || !to || from->kind == Ty::Kind::Error ||
        to->kind == Ty::Kind::Error || from->kind == Ty::Kind::Never)
      return true;
    if (same(from, to))
      return true;
    if (from->kind == Ty::Kind::Ref && to->kind == Ty::Kind::Ref) {
      if (from->is_mutable && !to->is_mutable &&
          same(from->element, to->element))
        return true;
      auto current = from;
      while (current->kind == Ty::Kind::Ref || current->kind == Ty::Kind::Box) {
        current = current->element;
        if (to->kind == Ty::Kind::Ref &&
            (!to->is_mutable || from->is_mutable) && same(current, to->element))
          return true;
      }
    }
    return false;
  }

  TyPtr common_type(const TyPtr &first, const TyPtr &second) {
    if (coercible(second, first))
      return first;
    if (coercible(first, second))
      return second;
    return ty(Ty::Kind::Error);
  }

  bool writable(const ExprInfo &value) const {
    if (value.access_locked)
      return false;
    auto current = value.type;
    while (current && current->kind == Ty::Kind::Box)
      current = current->element;
    if (current && current->kind == Ty::Kind::Ref)
      return current->is_mutable;
    return !value.is_place || value.is_mutable;
  }

  void mismatch(Span span, const TyPtr &actual, const TyPtr &expected) {
    report(span, "type mismatch: found " + type_name(actual) + ", expected " +
                     type_name(expected));
  }

  TyPtr literal_type(const ast::IntegerLiteral &literal, const TyPtr &expected,
                     Span span) {
    TyPtr result;
    if (literal.suffix) {
      switch (*literal.suffix) {
      case ast::IntegerSuffix::I32:
        result = ty(Ty::Kind::I32);
        break;
      case ast::IntegerSuffix::U32:
        result = ty(Ty::Kind::U32);
        break;
      case ast::IntegerSuffix::Isize:
        result = ty(Ty::Kind::Isize);
        break;
      case ast::IntegerSuffix::Usize:
        result = ty(Ty::Kind::Usize);
        break;
      }
    } else if (integer(expected))
      result = expected;
    else
      result = ty(Ty::Kind::I32);
    auto value = integer_magnitude(literal);
    if (!value || *value > std::numeric_limits<std::uint32_t>::max())
      report(span, "integer literal is outside the 32-bit target range");
    return result;
  }

  std::vector<Adjustment> coercion_adjustments(const TyPtr &from,
                                               const TyPtr &to) const {
    std::vector<Adjustment> result;
    if (!from || !to || same(from, to) || from->kind == Ty::Kind::Error ||
        to->kind == Ty::Kind::Error)
      return result;
    if (from->kind == Ty::Kind::Never) {
      result.push_back({Adjustment::Kind::NeverToAny, to});
      return result;
    }
    if (from->kind == Ty::Kind::Ref && to->kind == Ty::Kind::Ref &&
        from->is_mutable && !to->is_mutable &&
        same(from->element, to->element)) {
      result.push_back({Adjustment::Kind::MutToShared, to});
      return result;
    }
    if (from->kind == Ty::Kind::Ref && to->kind == Ty::Kind::Ref) {
      auto current = from;
      while (current->kind == Ty::Kind::Ref || current->kind == Ty::Kind::Box) {
        current = current->element;
        result.push_back({Adjustment::Kind::Dereference, current});
        if (same(current, to->element)) {
          result.push_back({to->is_mutable ? Adjustment::Kind::BorrowMutable
                                           : Adjustment::Kind::BorrowShared,
                            to});
          return result;
        }
      }
    }
    result.clear();
    return result;
  }

  void append_adjustments(ast::NodeId node,
                          std::vector<Adjustment> adjustments) {
    auto &fact = expression_facts_[node];
    fact.node = node;
    fact.adjustments.insert(fact.adjustments.end(),
                            std::make_move_iterator(adjustments.begin()),
                            std::make_move_iterator(adjustments.end()));
  }

  void record_node(const ast::Node &node, const ExprInfo &result) {
    expression_facts_[node.id] = {
        node.id,           result.type,          result.is_place,
        result.is_mutable, result.access_locked, result.symbol,
        result.target,     result.adjustments};
  }
  ExprInfo check_expr(const ast::ExprPtr &expression, TyPtr expected = {}) {
    return check_expr(*expression, std::move(expected));
  }
  ExprInfo check_expr(const ast::Expr &expression, TyPtr expected = {}) {
    auto result = ast::visit(
        expression,
        ast::Overloaded{
            [&](const ast::Expr &node, const ast::IntegerExpr &p) -> ExprInfo {
              return {literal_type(p.literal, expected, node.span)};
            },
            [&](const ast::Expr &, const ast::BooleanExpr &) -> ExprInfo {
              return {ty(Ty::Kind::Bool)};
            },
            [&](const ast::Expr &, const ast::UnitExpr &) -> ExprInfo {
              return {ty(Ty::Kind::Unit)};
            },
            [&](const ast::Expr &, const ast::ParenExpr &p) {
              auto inner = check_expr(p.inner, expected);
              inner.adjustments.clear();
              return inner;
            },
            [&](const ast::Expr &node, const ast::PathExpr &path) {
              return check_path(node, path);
            },
            [&](const ast::Expr &node, const ast::ArrayExpr &array) {
              return check_array(node, array, expected);
            },
            [&](const ast::Expr &node, const ast::RepeatArrayExpr &array) {
              return check_repeat_array(node, array, expected);
            },
            [&](const ast::Expr &node, const ast::StructExpr &structure) {
              return check_struct(node, structure);
            },
            [&](const ast::Expr &, const ast::BlockExpr &p) {
              return check_block(*p.block, expected);
            },
            [&](const ast::Expr &node, const ast::IfExpr &p) {
              return check_if(node, p, expected);
            },
            [&](const ast::Expr &node, const ast::LoopExpr &loop_expr) {
              return check_loop(node, loop_expr, expected);
            },
            [&](const ast::Expr &node, const ast::WhileExpr &while_expr) {
              return check_while(node, while_expr);
            },
            [&](const ast::Expr &node, const ast::UnaryExpr &unary) {
              return check_unary(node, unary, expected);
            },
            [&](const ast::Expr &node, const ast::BorrowExpr &borrow) {
              return check_borrow(node, borrow);
            },
            [&](const ast::Expr &node, const ast::BinaryExpr &binary) {
              return check_binary(node, binary);
            },
            [&](const ast::Expr &node, const ast::AssignExpr &assignment) {
              return check_assignment(node, assignment);
            },
            [&](const ast::Expr &node,
                const ast::CompoundAssignExpr &assignment) {
              return check_compound_assignment(node, assignment);
            },
            [&](const ast::Expr &node, const ast::CastExpr &cast) {
              return check_cast(node, cast);
            },
            [&](const ast::Expr &node, const ast::CallExpr &call) {
              return check_call(node, call);
            },
            [&](const ast::Expr &node, const ast::MethodCallExpr &method) {
              return check_method(node, method);
            },
            [&](const ast::Expr &node, const ast::FieldExpr &projection) {
              return check_field(node, projection);
            },
            [&](const ast::Expr &node, const ast::IndexExpr &projection) {
              return check_index(node, projection);
            },
            [&](const ast::Expr &node, const ast::BreakExpr &p) {
              return check_break(node, p);
            },
            [&](const ast::Expr &node, const ast::ContinueExpr &p) {
              return check_continue(node, p);
            },
            [&](const ast::Expr &node, const ast::ReturnExpr &p) {
              return check_return(node, p);
            }});
    if (expected && !ast::get_if<ast::ParenExpr>(expression) &&
        coercible(result.type, expected)) {
      auto adjustments = coercion_adjustments(result.type, expected);
      result.adjustments.insert(result.adjustments.end(), adjustments.begin(),
                                adjustments.end());
    }
    record_node(expression, result);
    return result;
  }

  ExprInfo check_path(const ast::Expr &node, const ast::PathExpr &value) {
    const auto &path = value.path;
    auto name = ast::path_name(path);
    if (path.segments.size() > 1 &&
        std::string(ast::name_text(path.segments.front().name)) == "Self" &&
        current_function_ && !current_function_->owner.empty()) {
      name = current_function_->owner;
      for (std::size_t i = 1; i < path.segments.size(); ++i)
        name += "::" + std::string(ast::name_text(path.segments[i].name));
    }
    if (path.segments.size() == 1) {
      if (auto *binding = lookup(name))
        return {binding->type,   true, binding->is_mutable, false,
                binding->symbol, name};
      if (auto it = constants_.find(name); it != constants_.end())
        return {it->second.type,
                false,
                false,
                false,
                symbol_for(it->second.ast->id),
                name};
      if (auto it = functions_.find(name); it != functions_.end()) {
        auto value = ty(Ty::Kind::Function);
        value->name = name;
        return {value, false, false, false, symbol_for(it->second.ast->id),
                name};
      }
      if (builtin_function(name)) {
        auto value = ty(Ty::Kind::Function);
        value->name = name;
        return {value, false, false, false, std::nullopt, "builtin::" + name};
      }
      if (auto it = structures_.find(name); it != structures_.end()) {
        auto value = ty(Ty::Kind::Function);
        value->name = name;
        return {value, false, false, false, symbol_for(it->second.ast->id),
                name};
      }
    } else {
      if (auto it = associated_constants_.find(name);
          it != associated_constants_.end())
        return {it->second.type,
                false,
                false,
                false,
                symbol_for(it->second.ast->id),
                name};
      if (auto it = associated_functions_.find(name);
          it != associated_functions_.end()) {
        auto value = ty(Ty::Kind::Function);
        value->name = name;
        return {value, false, false, false, symbol_for(it->second.ast->id),
                name};
      }
      if (constructor_path(path)) {
        auto value = ty(Ty::Kind::Function);
        value->name = name;
        std::optional<SymbolId> symbol;
        if (auto it = structures_.find(
                std::string(ast::name_text(path.segments.front().name)));
            it != structures_.end())
          symbol = symbol_for(it->second.ast->id);
        return {value, false, false, false, symbol, "builtin::" + name};
      }
    }
    report(node.span, "unresolved value name '" + name + "'");
    return {};
  }

  static bool builtin_function(std::string_view name) {
    return protected_builtin_value(name) || name == "get_i32" ||
           name == "print_i32" || name == "println_i32";
  }

  bool constructor_path(const ast::Path &path) {
    if (path.segments.size() != 2)
      return false;
    const std::string first{ast::name_text(path.segments[0].name)};
    auto second = ast::name_text(path.segments[1].name);
    const bool container = first == "Box" || first == "Vec";
    const bool member = second == "new" || second == "clone" ||
                        second == "len" || second == "is_empty" ||
                        second == "push" || second == "remove";
    return (container && member) ||
           (structures_.contains(first) && second == "clone");
  }

  ExprInfo check_array(const ast::Expr &node, const ast::ArrayExpr &array,
                       const TyPtr &expected) {
    auto expected_element = expected && expected->kind == Ty::Kind::Array
                                ? expected->element
                                : TyPtr{};
    if (array.elements.empty()) {
      if (!expected_element) {
        report(node.span, "an empty array requires an expected array type");
        return {};
      }
      return {compound(Ty::Kind::Array, expected_element)};
    }
    auto first = check_expr(array.elements.front(), expected_element);
    auto result = compound(Ty::Kind::Array, first.type);
    result->length = array.elements.size();
    for (std::size_t i = 1; i < array.elements.size(); ++i) {
      auto element = check_expr(array.elements[i], expected_element);
      auto merged = common_type(first.type, element.type);
      if (merged->kind == Ty::Kind::Error)
        mismatch(array.elements[i]->span, element.type, first.type);
      else {
        if (!same(element.type, merged))
          append_adjustments(array.elements[i]->id,
                             coercion_adjustments(element.type, merged));
        first.type = merged;
      }
    }
    result->element = first.type;
    if (expected_element &&
        (result->length != expected->length || !coercible(result, expected)))
      mismatch(node.span, result, expected);
    return {result};
  }
  ExprInfo check_repeat_array(const ast::Expr &node,
                              const ast::RepeatArrayExpr &array,
                              const TyPtr &expected) {
    auto expected_element = expected && expected->kind == Ty::Kind::Array
                                ? expected->element
                                : TyPtr{};
    auto first = check_expr(array.element, expected_element);
    auto result = compound(Ty::Kind::Array, first.type);
    check_expr(array.count, ty(Ty::Kind::Usize));
    auto length = evaluate_length(
        array.count, current_function_ ? current_function_->owner : "");
    result->length = length.value_or(0);
    if (result->length > 1 && !supports(first.type, "Copy", "", {}))
      report(node.span, "array repetition requires Copy");
    if (expected_element &&
        (result->length != expected->length || !coercible(result, expected)))
      mismatch(node.span, result, expected);
    return {result};
  }

  ExprInfo check_struct(const ast::Expr &node,
                        const ast::StructExpr &structure) {
    auto name = ast::path_name(structure.path);
    if (name == "Self" && current_function_ &&
        !current_function_->owner.empty())
      name = current_function_->owner;
    auto it = structures_.find(name);
    if (it == structures_.end()) {
      report(node.span,
             "struct literal names an unknown struct '" + name + "'");
      return {};
    }
    std::unordered_set<std::string> initialized;
    for (const auto &field : structure.fields) {
      if (!initialized.insert(field.name.text).second)
        report(field.span,
               "duplicate field initializer '" + field.name.text + "'");
      auto expected = it->second.fields.find(field.name.text);
      if (expected == it->second.fields.end()) {
        report(field.span, "unknown field '" + field.name.text + "'");
        check_expr(field.value);
      } else {
        auto value = check_expr(field.value, expected->second);
        if (!coercible(value.type, expected->second))
          mismatch(field.value->span, value.type, expected->second);
      }
    }
    for (const auto &[field, _] : it->second.fields)
      if (!initialized.contains(field))
        report(node.span, "missing field initializer '" + field + "'");
    auto result = ty(Ty::Kind::Struct);
    result->name = name;
    return {result, false, false, false, symbol_for(it->second.ast->id), name};
  }

  ExprInfo check_block(const ast::Block &block, const TyPtr &expected = {}) {
    scopes_.emplace_back();
    TyPtr result = ty(Ty::Kind::Unit);
    for (std::size_t i = 0; i < block.statements.size(); ++i) {
      const auto &statement = block.statements[i];
      ast::visit(
          *statement,
          ast::Overloaded{
              [&](const ast::Stmt &node, const ast::EmptyStmt &) {},
              [&](const ast::Stmt &node, const ast::LetStmt &p) {
                auto annotation =
                    p.annotation
                        ? resolve_type(
                              *p.annotation,
                              current_function_ ? current_function_->owner : "")
                        : TyPtr{};
                auto initializer = check_expr(p.initializer, annotation);
                auto binding_type = annotation ? annotation : initializer.type;
                if (annotation && !coercible(initializer.type, annotation))
                  mismatch(p.initializer->span, initializer.type, annotation);
                if (annotation && annotation->kind == Ty::Kind::Ref &&
                    annotation->is_mutable && initializer.access_locked)
                  report(p.initializer->span,
                         "mutable reference initializer requires mutable "
                         "container access");
                record_symbol(
                    node.id, SymbolKind::Local, p.binding.name.text,
                    current_function_
                        ? std::optional<ast::NodeId>(current_function_->ast->id)
                        : std::nullopt);
                record_symbol_type(node.id, binding_type);
                if (protected_builtin_value(p.binding.name.text))
                  report(p.binding.name.span,
                         "cannot shadow a protected builtin");
                scopes_.back()[p.binding.name.text] = {
                    binding_type,
                    p.binding.mutability == ast::Mutability::Mutable,
                    symbol_for(node.id)};
              },
              [&](const ast::Stmt &node, const ast::ExprStmt &p) {
                auto value = check_expr(p.expression);
                if (!block.tail && i + 1 == block.statements.size() &&
                    value.type->kind == Ty::Kind::Never)
                  result = value.type;
                else if (p.termination == ast::Termination::Omitted &&
                         value.type->kind != Ty::Kind::Unit &&
                         value.type->kind != Ty::Kind::Never)
                  report(p.expression->span, "a non-final expression without a "
                                             "semicolon must have unit type");
              }});
    }
    if (block.tail)
      result = check_expr(*block.tail, expected).type;
    scopes_.pop_back();
    record_node(block, ExprInfo{result});
    return {result};
  }

  ExprInfo check_if(const ast::Node &node, const ast::IfExpr &branch,
                    const TyPtr &expected) {
    auto condition = check_expr(branch.condition, ty(Ty::Kind::Bool));
    if (!same(condition.type, ty(Ty::Kind::Bool)))
      mismatch(branch.condition->span, condition.type, ty(Ty::Kind::Bool));
    auto then_value = check_block(*branch.then_block, expected);
    if (!branch.else_branch) {
      if (then_value.type->kind != Ty::Kind::Unit &&
          then_value.type->kind != Ty::Kind::Never)
        report(node.span, "if without else requires a unit consequent");
      return {ty(Ty::Kind::Unit)};
    }
    ast::NodeId else_id;
    auto else_value =
        std::visit(ast::Overloaded{[&](const ast::Box<ast::Block> &p) {
                                     else_id = p->id;
                                     return check_block(*p, expected);
                                   },
                                   [&](const ast::Box<ast::IfBranch> &p) {
                                     else_id = p->id;
                                     auto result = ast::visit(
                                         *p, [&](const ast::IfBranch &owner,
                                                 const ast::IfExpr &payload) {
                                           return check_if(owner, payload,
                                                           expected);
                                         });
                                     record_node(*p, result);
                                     return result;
                                   }},
                   *branch.else_branch);
    if (then_value.type->kind == Ty::Kind::Never)
      return else_value;
    if (else_value.type->kind == Ty::Kind::Never)
      return then_value;
    if (coercible(else_value.type, then_value.type)) {
      append_adjustments(
          else_id, coercion_adjustments(else_value.type, then_value.type));
      return then_value;
    }
    if (coercible(then_value.type, else_value.type)) {
      append_adjustments(
          branch.then_block->id,
          coercion_adjustments(then_value.type, else_value.type));
      return else_value;
    }
    mismatch(node.span, else_value.type, then_value.type);
    return {};
  }

  ExprInfo check_loop(const ast::Expr &node, const ast::LoopExpr &loop_expr,
                      const TyPtr &expected) {
    loops_.push_back({expected, {}});
    auto body = check_block(*loop_expr.body);
    if (body.type->kind != Ty::Kind::Unit && body.type->kind != Ty::Kind::Never)
      report(loop_expr.body->span, "loop body must have unit type");
    auto loop = std::move(loops_.back());
    loops_.pop_back();
    if (loop.break_types.empty())
      return {ty(Ty::Kind::Never)};
    auto result = loop.expected ? loop.expected : loop.break_types.front();
    for (const auto &value : loop.break_types)
      if (!coercible(value, result))
        mismatch(node.span, value, result);
    return {result};
  }

  ExprInfo check_while(const ast::Expr &node,
                       const ast::WhileExpr &while_expr) {
    // A condition may only break or continue loops nested inside itself.
    auto surrounding_loops = std::move(loops_);
    loops_.clear();
    auto condition = check_expr(while_expr.condition, ty(Ty::Kind::Bool));
    loops_ = std::move(surrounding_loops);
    if (!same(condition.type, ty(Ty::Kind::Bool)))
      mismatch(while_expr.condition->span, condition.type, ty(Ty::Kind::Bool));
    loops_.push_back({ty(Ty::Kind::Unit), {}});
    auto body = check_block(*while_expr.body, ty(Ty::Kind::Unit));
    if (body.type->kind != Ty::Kind::Unit && body.type->kind != Ty::Kind::Never)
      report(while_expr.body->span, "while body must have unit type");
    for (const auto &value : loops_.back().break_types)
      if (value->kind != Ty::Kind::Unit)
        report(node.span, "break with a value is not allowed in while");
    loops_.pop_back();
    return {ty(Ty::Kind::Unit)};
  }

  ExprInfo check_borrow(const ast::Expr &node, const ast::BorrowExpr &borrow) {
    auto operand = check_expr(borrow.operand);
    bool mutable_ref = borrow.mutability == ast::Mutability::Mutable;
    if (mutable_ref &&
        ((operand.is_place && !operand.is_mutable) || operand.access_locked))
      report(borrow.operator_span, "mutable borrow requires a mutable place");
    return {compound(Ty::Kind::Ref, operand.type, mutable_ref)};
  }
  ExprInfo check_unary(const ast::Expr &node, const ast::UnaryExpr &unary,
                       const TyPtr &expected) {
    auto op = unary.op;
    auto operand = check_expr(unary.operand, expected);
    if (op == ast::UnaryOp::Dereference) {
      if (operand.type->kind != Ty::Kind::Ref &&
          operand.type->kind != Ty::Kind::Box) {
        report(node.span,
               "type " + type_name(operand.type) + " cannot be dereferenced");
        return {};
      }
      const bool shared_layer =
          operand.type->kind == Ty::Kind::Ref && !operand.type->is_mutable;
      const bool locked = operand.access_locked || shared_layer;
      const bool mut =
          (operand.type->kind == Ty::Kind::Ref ? operand.type->is_mutable
                                               : operand.is_mutable) &&
          !locked;
      return {operand.type->element, true, mut, locked};
    }
    TyPtr scalar = operand.type;
    if (scalar->kind == Ty::Kind::Ref && !scalar->is_mutable)
      scalar = scalar->element;
    if (op == ast::UnaryOp::Not) {
      if (!integer(scalar) && scalar->kind != Ty::Kind::Bool)
        report(node.span, "operator ! requires bool or integer");
      return {scalar};
    }
    if (op == ast::UnaryOp::Negate && !signed_integer(scalar))
      report(node.span, "unary minus requires a signed integer");
    return {scalar};
  }

  ExprInfo check_binary(const ast::Expr &node, const ast::BinaryExpr &binary) {
    auto left = check_expr(binary.left);
    auto right = check_expr(binary.right, left.type);
    auto lhs = scalar_operand(left.type);
    auto rhs = scalar_operand(right.type);
    auto op = binary.op;
    if (op == ast::BinaryOp::LogicalAnd || op == ast::BinaryOp::LogicalOr) {
      if (left.type->kind != Ty::Kind::Bool ||
          right.type->kind != Ty::Kind::Bool)
        report(node.span, "logical operators require bool operands");
      return {ty(Ty::Kind::Bool)};
    }
    if (op == ast::BinaryOp::Eq || op == ast::BinaryOp::Ne) {
      if (!same(lhs, rhs))
        mismatch(node.span, rhs, lhs);
      if (!supports(lhs, "PartialEq", "", {}))
        report(node.span, "equality requires PartialEq");
      return {ty(Ty::Kind::Bool)};
    }
    if (op == ast::BinaryOp::Lt || op == ast::BinaryOp::Le ||
        op == ast::BinaryOp::Gt || op == ast::BinaryOp::Ge) {
      auto ordering_left = left.type;
      auto ordering_right = right.type;
      if ((ordering_left->kind == Ty::Kind::Ref) !=
          (ordering_right->kind == Ty::Kind::Ref)) {
        report(node.span, "ordering requires matching scalar operands");
        return {ty(Ty::Kind::Bool)};
      }
      while (ordering_left->kind == Ty::Kind::Ref &&
             ordering_right->kind == Ty::Kind::Ref) {
        if (ordering_left->is_mutable && !ordering_right->is_mutable)
          report(node.span, "ordering cannot coerce a mutable left "
                            "reference to a shared right reference");
        ordering_left = ordering_left->element;
        ordering_right = ordering_right->element;
      }
      if ((!integer(ordering_left) && ordering_left->kind != Ty::Kind::Bool) ||
          !same(ordering_left, ordering_right))
        report(node.span, "ordering requires matching scalar operands");
      return {ty(Ty::Kind::Bool)};
    }
    if (op == ast::BinaryOp::Shl || op == ast::BinaryOp::Shr) {
      if (!integer(lhs) || !integer(rhs))
        report(node.span, "shift operands must be integers");
      return {lhs};
    }
    if ((op == ast::BinaryOp::BitAnd || op == ast::BinaryOp::BitOr ||
         op == ast::BinaryOp::BitXor) &&
        lhs->kind == Ty::Kind::Bool && rhs->kind == Ty::Kind::Bool)
      return {ty(Ty::Kind::Bool)};
    if (!integer(lhs) || !same(lhs, rhs))
      report(node.span, "integer operator requires matching integer operands");
    return {lhs};
  }

  TyPtr scalar_operand(TyPtr value) {
    if (value->kind == Ty::Kind::Ref && !value->is_mutable)
      return value->element;
    return value;
  }

  ExprInfo check_assignment(const ast::Expr &node,
                            const ast::AssignExpr &assignment) {
    auto destination = check_expr(assignment.target);
    auto source = check_expr(assignment.value, destination.type);
    check_assignment_target(assignment.target, destination);
    if (!coercible(source.type, destination.type))
      mismatch(assignment.value->span, source.type, destination.type);
    return {ty(Ty::Kind::Unit)};
  }
  void check_assignment_target(const ast::ExprPtr &target,
                               const ExprInfo &destination) {
    if (!destination.is_place)
      report(target->span, "assignment destination is not a place");
    else if (!destination.is_mutable)
      report(target->span, "assignment destination is immutable");
  }
  ExprInfo
  check_compound_assignment(const ast::Expr &node,
                            const ast::CompoundAssignExpr &assignment) {
    auto destination = check_expr(assignment.target);
    auto source = check_expr(assignment.value, destination.type);
    check_assignment_target(assignment.target, destination);
    auto scalar = scalar_operand(source.type);
    bool shift = assignment.op == ast::CompoundAssignOp::Shl ||
                 assignment.op == ast::CompoundAssignOp::Shr;
    if (!(shift && integer(scalar) && integer(destination.type)) &&
        !same(scalar, destination.type))
      mismatch(assignment.value->span, scalar, destination.type);
    if (!integer(destination.type) && destination.type->kind != Ty::Kind::Bool)
      report(node.span, "compound assignment requires a scalar destination");
    return {ty(Ty::Kind::Unit)};
  }

  ExprInfo check_cast(const ast::Expr &node, const ast::CastExpr &cast) {
    auto source = check_expr(cast.value);
    auto destination = resolve_type(
        cast.target_type, current_function_ ? current_function_->owner : "");
    const bool valid =
        (integer(source.type) && integer(destination)) ||
        (source.type->kind == Ty::Kind::Bool && integer(destination));
    if (!valid)
      report(node.span, "unsupported cast from " + type_name(source.type) +
                            " to " + type_name(destination));
    return {destination};
  }

  ExprInfo check_call(const ast::Expr &node, const ast::CallExpr &call) {
    auto callee = check_expr(call.callee);
    auto *path = ast::get_if<ast::PathExpr>(*ast::unparen(call.callee));
    auto arguments =
        path ? type_arguments(path->path) : std::vector<const ast::TypePtr *>{};
    if (callee.type->kind != Ty::Kind::Function) {
      report(node.span, "called expression is not a function");
      for (const auto &argument : call.arguments)
        check_expr(argument);
      return {};
    }
    const auto &name = callee.type->name;
    auto finish = [&](ExprInfo result) {
      result.symbol = callee.symbol;
      result.target = callee.target.empty() ? name : callee.target;
      return result;
    };
    if (!arguments.empty() && !name.starts_with("Box::") &&
        !name.starts_with("Vec::"))
      report(call.callee->span, "functions have no type parameters");
    if (name == "get_i32" || name == "getInt")
      return finish(
          check_signature(node, call.arguments, {}, ty(Ty::Kind::I32)));
    if (name == "print_i32" || name == "println_i32" || name == "printInt" ||
        name == "printlnInt")
      return finish(check_signature(node, call.arguments, {ty(Ty::Kind::I32)},
                                    ty(Ty::Kind::Unit)));
    if (auto it = functions_.find(name); it != functions_.end())
      return finish(check_signature(node, call.arguments, it->second.parameters,
                                    it->second.result));
    if (auto it = associated_functions_.find(name);
        it != associated_functions_.end()) {
      auto parameters = it->second.parameters;
      if (it->second.ast->signature.receiver.has_value()) {
        auto self = ty(Ty::Kind::Struct);
        self->name = it->second.owner;
        if (ast::by_reference(*it->second.ast->signature.receiver))
          self = compound(Ty::Kind::Ref, self,
                          ast::is_mutable(*it->second.ast->signature.receiver));
        parameters.insert(parameters.begin(), self);
      }
      return finish(
          check_signature(node, call.arguments, parameters, it->second.result));
    }
    if (name == "Box::new" || name == "Vec::new") {
      if (arguments.size() != 1) {
        report(node.span,
               name.substr(0, 3) + " constructor requires one type argument");
        return finish({});
      }
      auto element = resolve_type(*arguments.front());
      if (name == "Box::new")
        return finish(check_signature(node, call.arguments, {element},
                                      compound(Ty::Kind::Box, element)));
      return finish(check_signature(node, call.arguments, {},
                                    compound(Ty::Kind::Vec, element)));
    }
    if (name == "Box::clone" || name == "Vec::clone" || name == "Vec::len" ||
        name == "Vec::is_empty" || name == "Vec::push" ||
        name == "Vec::remove") {
      if (arguments.size() != 1) {
        report(node.span,
               "container associated function requires one type argument");
        return finish({});
      }
      auto element = resolve_type(*arguments.front());
      auto container = compound(
          name.starts_with("Box") ? Ty::Kind::Box : Ty::Kind::Vec, element);
      if (name.ends_with("clone")) {
        if (!supports(container, "Clone", "", {}))
          report(node.span,
                 "clone is not available for " + type_name(container));
        return finish(check_signature(node, call.arguments,
                                      {compound(Ty::Kind::Ref, container)},
                                      container));
      }
      if (name.ends_with("push"))
        return finish(
            check_signature(node, call.arguments,
                            {compound(Ty::Kind::Ref, container, true), element},
                            ty(Ty::Kind::Unit)));
      if (name.ends_with("remove"))
        return finish(check_signature(
            node, call.arguments,
            {compound(Ty::Kind::Ref, container, true), ty(Ty::Kind::Usize)},
            element));
      return finish(check_signature(
          node, call.arguments, {compound(Ty::Kind::Ref, container)},
          name.ends_with("len") ? ty(Ty::Kind::Usize) : ty(Ty::Kind::Bool)));
    }
    if (name.ends_with("::clone")) {
      auto owner = name.substr(0, name.size() - 7);
      auto result = ty(Ty::Kind::Struct);
      result->name = owner;
      return finish(check_signature(node, call.arguments,
                                    {compound(Ty::Kind::Ref, result)}, result));
    }
    report(node.span, "unknown function '" + name + "'");
    return finish({});
  }

  ExprInfo check_signature(const ast::Node &node,
                           const std::vector<ast::ExprPtr> &arguments,
                           const std::vector<TyPtr> &parameters, TyPtr result) {
    const auto count = arguments.size();
    if (count != parameters.size())
      report(node.span, "wrong number of function arguments");
    for (std::size_t i = 0; i < count; ++i) {
      auto expected = i < parameters.size() ? parameters[i] : TyPtr{};
      auto argument = check_expr(arguments[i], expected);
      if (expected && !coercible(argument.type, expected))
        mismatch(arguments[i]->span, argument.type, expected);
      if (expected && expected->kind == Ty::Kind::Ref && expected->is_mutable &&
          argument.access_locked)
        report(arguments[i]->span,
               "mutable reference argument requires mutable container access");
    }
    return {std::move(result)};
  }

  ExprInfo check_method(const ast::Expr &node,
                        const ast::MethodCallExpr &method) {
    if (!type_arguments(method.generic_args).empty())
      report(node.span, "method segments have no type parameters");
    auto receiver = check_expr(method.receiver);
    TyPtr base = receiver.type;
    std::vector<Adjustment> receiver_adjustments;
    while (base->kind == Ty::Kind::Ref || base->kind == Ty::Kind::Box) {
      base = base->element;
      receiver_adjustments.push_back({Adjustment::Kind::Dereference, base});
    }
    auto finish = [&](ExprInfo result, std::string target,
                      std::optional<SymbolId> symbol = {},
                      std::optional<Adjustment::Kind> borrow = {}) {
      result.target = std::move(target);
      result.symbol = symbol;
      result.adjustments = receiver_adjustments;
      if (borrow) {
        const bool mutable_borrow = *borrow == Adjustment::Kind::BorrowMutable;
        result.adjustments.push_back(
            {*borrow, compound(Ty::Kind::Ref, base, mutable_borrow)});
      }
      return result;
    };
    const auto &name = method.method.text;
    if (name == "len" || name == "is_empty") {
      if (receiver.type->kind != Ty::Kind::Vec &&
          receiver.type->kind != Ty::Kind::Array &&
          !(receiver.type->kind == Ty::Kind::Ref &&
            (receiver.type->element->kind == Ty::Kind::Vec ||
             receiver.type->element->kind == Ty::Kind::Array)))
        report(node.span, name + " is only available on arrays and Vec");
      return finish(check_method_signature(node, method.arguments, {},
                                           name == "len" ? ty(Ty::Kind::Usize)
                                                         : ty(Ty::Kind::Bool)),
                    "builtin::" + name, {}, Adjustment::Kind::BorrowShared);
    }
    if (name == "push" || name == "remove") {
      TyPtr vector = receiver.type;
      if (vector->kind == Ty::Kind::Ref)
        vector = vector->element;
      if (vector->kind != Ty::Kind::Vec) {
        report(node.span, name + " is only available on Vec");
        return finish({}, "builtin::Vec::" + name);
      }
      if (!writable(receiver))
        report(node.span, name + " requires mutable access to Vec");
      if (name == "push")
        return finish(
            check_method_signature(node, method.arguments, {vector->element},
                                   ty(Ty::Kind::Unit)),
            "builtin::Vec::push", {}, Adjustment::Kind::BorrowMutable);
      return finish(
          check_method_signature(node, method.arguments, {ty(Ty::Kind::Usize)},
                                 vector->element),
          "builtin::Vec::remove", {}, Adjustment::Kind::BorrowMutable);
    }
    if (name == "clone") {
      auto cloned = receiver.type;
      if (cloned->kind == Ty::Kind::Ref &&
          (cloned->element->kind == Ty::Kind::Box ||
           cloned->element->kind == Ty::Kind::Vec ||
           cloned->element->kind == Ty::Kind::Array))
        cloned = cloned->element;
      if (!supports(cloned, "Clone", "", {}))
        report(node.span, "clone is not available for " + type_name(cloned));
      return finish(check_method_signature(node, method.arguments, {}, cloned),
                    "builtin::clone", {}, Adjustment::Kind::BorrowShared);
    }
    if (base->kind == Ty::Kind::Struct) {
      auto key = base->name + "::" + name;
      if (auto it = associated_functions_.find(key);
          it != associated_functions_.end()) {
        const auto &function = it->second;
        if (!function.ast->signature.receiver.has_value()) {
          report(node.span, "associated function without self cannot be "
                            "called with dot syntax");
          return finish({}, key, symbol_for(function.ast->id));
        }
        if (ast::by_reference(*function.ast->signature.receiver) &&
            ast::is_mutable(*function.ast->signature.receiver) &&
            !writable(receiver))
          report(node.span, "method requires a mutable receiver");
        return finish(
            check_method_signature(node, method.arguments, function.parameters,
                                   function.result),
            key, symbol_for(function.ast->id),
            ast::by_reference(*function.ast->signature.receiver)
                ? std::optional<Adjustment::Kind>(
                      ast::is_mutable(*function.ast->signature.receiver)
                          ? Adjustment::Kind::BorrowMutable
                          : Adjustment::Kind::BorrowShared)
                : std::nullopt);
      }
    }
    report(node.span, "unknown method '" + name + "'");
    return finish({}, name);
  }

  ExprInfo check_method_signature(const ast::Node &node,
                                  const std::vector<ast::ExprPtr> &arguments,
                                  const std::vector<TyPtr> &parameters,
                                  TyPtr result) {
    const auto count = arguments.size();
    if (count != parameters.size())
      report(node.span, "wrong number of method arguments");
    for (std::size_t i = 0; i < count; ++i) {
      auto expected = i < parameters.size() ? parameters[i] : TyPtr{};
      auto value = check_expr(arguments[i], expected);
      if (expected && !coercible(value.type, expected))
        mismatch(arguments[i]->span, value.type, expected);
    }
    return {std::move(result)};
  }

  ExprInfo check_field(const ast::Expr &node,
                       const ast::FieldExpr &projection) {
    auto receiver = check_expr(projection.base);
    auto base = receiver.type;
    bool mutable_access = receiver.is_place ? receiver.is_mutable : true;
    bool locked = receiver.access_locked;
    std::vector<Adjustment> adjustments;
    while (base->kind == Ty::Kind::Ref || base->kind == Ty::Kind::Box) {
      if (base->kind == Ty::Kind::Ref) {
        if (!base->is_mutable) {
          locked = true;
          mutable_access = false;
        } else if (!locked) {
          // Binding immutability does not remove an &mut referent's rights.
          mutable_access = true;
        }
      }
      base = base->element;
      adjustments.push_back({Adjustment::Kind::Dereference, base});
    }
    if (base->kind != Ty::Kind::Struct || !structures_.contains(base->name)) {
      report(node.span, "field access requires a struct");
      return {};
    }
    auto field = structures_.at(base->name).fields.find(projection.field.text);
    if (field == structures_.at(base->name).fields.end()) {
      report(node.span, "unknown field '" + projection.field.text + "'");
      return {};
    }
    return {field->second,
            true,
            mutable_access,
            locked,
            structures_.at(base->name).field_symbols.at(projection.field.text),
            base->name + "::" + projection.field.text,
            std::move(adjustments)};
  }

  ExprInfo check_index(const ast::Expr &node,
                       const ast::IndexExpr &projection) {
    auto base = check_expr(projection.base);
    auto index = check_expr(projection.index, ty(Ty::Kind::Usize));
    if (!same(index.type, ty(Ty::Kind::Usize)))
      mismatch(projection.index->span, index.type, ty(Ty::Kind::Usize));
    auto container = base.type;
    bool mutable_access = base.is_place ? base.is_mutable : true;
    bool locked = base.access_locked;
    std::vector<Adjustment> adjustments;
    while (container->kind == Ty::Kind::Ref ||
           container->kind == Ty::Kind::Box) {
      if (container->kind == Ty::Kind::Ref) {
        if (!container->is_mutable) {
          locked = true;
          mutable_access = false;
        } else if (!locked) {
          mutable_access = true;
        }
      }
      container = container->element;
      adjustments.push_back({Adjustment::Kind::Dereference, container});
    }
    if (container->kind != Ty::Kind::Array &&
        container->kind != Ty::Kind::Vec) {
      report(node.span, "type " + type_name(container) + " cannot be indexed");
      return {};
    }
    if (container->kind == Ty::Kind::Vec && !mutable_access)
      locked = true;
    return {container->element, true,    mutable_access,        locked,
            std::nullopt,       "index", std::move(adjustments)};
  }

  ExprInfo check_break(const ast::Expr &node, const ast::BreakExpr &p) {
    if (loops_.empty()) {
      report(node.span, "break is only available inside a loop");
      return {ty(Ty::Kind::Never)};
    }
    auto value = p.value ? check_expr(*p.value, loops_.back().expected)
                         : ExprInfo{ty(Ty::Kind::Unit)};
    loops_.back().break_types.push_back(value.type);
    return {ty(Ty::Kind::Never)};
  }

  ExprInfo check_continue(const ast::Expr &node, const ast::ContinueExpr &) {
    if (loops_.empty())
      report(node.span, "continue is only available inside a loop");
    return {ty(Ty::Kind::Never)};
  }

  ExprInfo check_return(const ast::Expr &node, const ast::ReturnExpr &p) {
    auto expected =
        current_function_ ? current_function_->result : ty(Ty::Kind::Error);
    auto value =
        p.value ? check_expr(*p.value, expected) : ExprInfo{ty(Ty::Kind::Unit)};
    if (!coercible(value.type, expected))
      mismatch(node.span, value.type, expected);
    return {ty(Ty::Kind::Never)};
  }

  struct LoopInfo {
    TyPtr expected;
    std::vector<TyPtr> break_types;
  };

  const ast::Crate &crate_;
  DiagnosticEngine &diagnostics_;
  std::unordered_map<std::string, StructInfo> structures_;
  std::unordered_map<std::string, FunctionInfo> functions_;
  std::unordered_map<std::string, ConstantInfo> constants_;
  std::vector<const ast::Impl *> impls_;
  std::unordered_map<std::string, FunctionInfo> associated_functions_;
  std::unordered_map<std::string, ConstantInfo> associated_constants_;
  std::vector<std::unordered_map<std::string, Binding>> scopes_;
  std::vector<LoopInfo> loops_;
  const FunctionInfo *current_function_{};
  SemanticResult result_;
  SymbolId next_symbol_{1};
  std::map<ast::NodeId, SymbolId> declaration_symbols_;
  std::unordered_set<SymbolId> recorded_symbols_;
  std::map<ast::NodeId, ExpressionSemantics> expression_facts_;
  std::map<ast::NodeId, TyPtr> resolved_types_;
  std::map<SymbolId, TyPtr> symbol_types_;
};

} // namespace

std::string_view symbol_kind_name(SymbolKind kind) {
  constexpr std::array names{"struct",
                             "function",
                             "constant",
                             "associated-function",
                             "associated-constant",
                             "field",
                             "parameter",
                             "receiver",
                             "local"};
  return names.at(static_cast<std::size_t>(kind));
}
SemanticResult analyze(const ast::Crate &crate, DiagnosticEngine &diagnostics) {
  return Analyzer(crate, diagnostics).run();
}

std::string dump_semantic(const SemanticResult &result) {
  std::ostringstream out;
  out << "Semantic\n"
      << "  Summary functions=" << result.summary.functions
      << " structures=" << result.summary.structures
      << " constants=" << result.summary.constants << '\n';
  out << "  Symbols\n";
  for (const auto &symbol : result.symbols) {
    out << "    #" << symbol.id << ' ' << symbol_kind_name(symbol.kind) << ' '
        << symbol.name << " declaration=#" << symbol.declaration;
    if (symbol.owner)
      out << " owner=#" << *symbol.owner;
    out << '\n';
  }
  out << "  Expressions\n";
  for (const auto &expression : result.expressions) {
    out << "    #" << expression.node << " type=" << type_name(expression.type)
        << (expression.is_place ? " place" : " value");
    if (expression.is_place)
      out << (expression.is_mutable ? " mutable" : " immutable");
    if (expression.access_locked)
      out << " access-locked";
    if (expression.symbol)
      out << " symbol=#" << *expression.symbol;
    if (!expression.target.empty())
      out << " target=" << expression.target;
    if (!expression.adjustments.empty()) {
      out << " adjustments=[";
      for (std::size_t i = 0; i < expression.adjustments.size(); ++i) {
        if (i)
          out << ", ";
        const auto &adjustment = expression.adjustments[i];
        switch (adjustment.kind) {
        case Adjustment::Kind::Dereference:
          out << "deref";
          break;
        case Adjustment::Kind::BorrowShared:
          out << "borrow-shared";
          break;
        case Adjustment::Kind::BorrowMutable:
          out << "borrow-mutable";
          break;
        case Adjustment::Kind::MutToShared:
          out << "mut-to-shared";
          break;
        case Adjustment::Kind::NeverToAny:
          out << "never-to-any";
          break;
        }
        if (adjustment.target)
          out << "->" << type_name(adjustment.target);
      }
      out << ']';
    }
    out << '\n';
  }
  return out.str();
}

} // namespace rx
