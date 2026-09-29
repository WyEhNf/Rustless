#include "rx/semantic.hpp"

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
    for (auto &[node, type] : resolved_types_)
      result_.resolved_types.push_back({node, std::move(type)});
    for (const auto &[_, constant] : constants_)
      if (constant.state == ConstantInfo::State::Done)
        result_.constants.push_back(
            {constant.ast->id, static_cast<std::uint32_t>(constant.value)});
    for (const auto &[_, constant] : associated_constants_)
      if (constant.state == ConstantInfo::State::Done)
        result_.constants.push_back(
            {constant.ast->id, static_cast<std::uint32_t>(constant.value)});
    std::sort(result_.constants.begin(), result_.constants.end(),
              [](const auto &a, const auto &b) { return a.symbol < b.symbol; });
    return std::move(result_);
  }

private:
  void report(Span span, std::string message) {
    diagnostics_.error(span, std::move(message));
  }

  void record_symbol(SymbolId id, std::string kind, std::string name,
                     std::optional<SymbolId> owner = {}) {
    if (recorded_symbols_.insert(id).second)
      result_.symbols.push_back({id, std::move(kind), std::move(name), owner});
  }

  void collect_items() {
    std::unordered_set<std::string> type_names;
    std::unordered_set<std::string> value_names;
    for (const auto &item : crate_.items) {
      std::visit(
          [&](const auto &value) {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, ast::Struct>) {
              record_symbol(value.id, "struct", value.name);
              if (builtin_type(value.name))
                report(value.span, "the builtin type name '" + value.name +
                                       "' is protected");
              if (!type_names.insert(value.name).second)
                report(value.span, "duplicate type name '" + value.name + "'");
              structures_[value.name].ast = &value;
            } else if constexpr (std::is_same_v<T, ast::Function>) {
              record_symbol(value.id, "function", value.name);
              if (protected_builtin_value(value.name))
                report(value.span, "the builtin value name '" + value.name +
                                       "' is protected");
              if (!value_names.insert(value.name).second)
                report(value.span, "duplicate value name '" + value.name + "'");
              functions_[value.name].ast = &value;
            } else if constexpr (std::is_same_v<T, ast::Constant>) {
              record_symbol(value.id, "constant", value.name);
              if (protected_builtin_value(value.name))
                report(value.span, "the builtin value name '" + value.name +
                                       "' is protected");
              if (!value_names.insert(value.name).second)
                report(value.span, "duplicate value name '" + value.name + "'");
              constants_[value.name].ast = &value;
            } else if constexpr (std::is_same_v<T, ast::Impl>) {
              impls_.push_back(&value);
            } else if constexpr (std::is_same_v<T, ast::Use>) {
              (void)value;
            }
          },
          item);
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
    if (syntax)
      resolved_types_[syntax->id] = result;
    return result;
  }

  TyPtr resolve_type_impl(const ast::TypePtr &syntax,
                          std::string_view self_name) {
    // Resolve names and validate generic arity at the declaration boundary.
    if (!syntax)
      return ty(Ty::Kind::Unit);
    if (syntax->kind == ast::Type::Kind::Unit)
      return ty(Ty::Kind::Unit);
    if (syntax->kind == ast::Type::Kind::Reference) {
      return compound(Ty::Kind::Ref, resolve_type(syntax->element, self_name),
                      syntax->is_mutable);
    }
    if (syntax->kind == ast::Type::Kind::Array) {
      auto value =
          compound(Ty::Kind::Array, resolve_type(syntax->element, self_name));
      auto length =
          evaluate_length(syntax->array_length, syntax->span, self_name);
      if (length)
        value->length = *length;
      return value;
    }
    const std::string name = syntax->path.empty() ? "" : syntax->path.back();
    if (name == "Self") {
      if (self_name.empty()) {
        report(syntax->span, "Self is only available inside a struct or impl");
        return ty(Ty::Kind::Error);
      }
      auto value = ty(Ty::Kind::Struct);
      value->name = std::string(self_name);
      return value;
    }
    static const std::unordered_map<std::string, Ty::Kind> primitives = {
        {"bool", Ty::Kind::Bool},
        {"i32", Ty::Kind::I32},
        {"u32", Ty::Kind::U32},
        {"isize", Ty::Kind::Isize},
        {"usize", Ty::Kind::Usize}};
    if (auto it = primitives.find(name); it != primitives.end()) {
      if (!syntax->arguments.empty())
        report(syntax->span,
               "primitive type '" + name + "' takes no type arguments");
      return ty(it->second);
    }
    if (name == "Box" || name == "Vec") {
      if (syntax->arguments.size() != 1) {
        report(syntax->span,
               name + " requires exactly one concrete type argument");
        return ty(Ty::Kind::Error);
      }
      return compound(name == "Box" ? Ty::Kind::Box : Ty::Kind::Vec,
                      resolve_type(syntax->arguments.front(), self_name));
    }
    if (structures_.contains(name)) {
      if (!syntax->arguments.empty())
        report(syntax->span, "struct '" + name + "' has no type parameters");
      auto value = ty(Ty::Kind::Struct);
      value->name = name;
      return value;
    }
    report(syntax->span, "unknown type '" + name + "'");
    return ty(Ty::Kind::Error);
  }

  std::optional<std::uint64_t>
  evaluate_length(std::string text, Span span,
                  std::string_view self_name = {}) {
    while (text.size() >= 2 && text.front() == '(' && text.back() == ')')
      text = text.substr(1, text.size() - 2);
    if (!self_name.empty() && text.starts_with("Self::"))
      text = std::string(self_name) + text.substr(4);
    if (auto it = constants_.find(text); it != constants_.end()) {
      resolve_constant_type(it->second);
      evaluate_constant(it->second);
      if (!it->second.type || it->second.type->kind != Ty::Kind::Usize)
        report(span, "array length must have type usize");
      if (it->second.state == ConstantInfo::State::Done &&
          it->second.value >= 0)
        return static_cast<std::uint64_t>(it->second.value);
      return std::nullopt;
    }
    if (auto it = associated_constants_.find(text);
        it != associated_constants_.end()) {
      resolve_constant_type(it->second);
      evaluate_constant(it->second);
      if (!it->second.type || it->second.type->kind != Ty::Kind::Usize)
        report(span, "array length must have type usize");
      if (it->second.state == ConstantInfo::State::Done &&
          it->second.value >= 0)
        return static_cast<std::uint64_t>(it->second.value);
      return std::nullopt;
    }
    std::string digits = text;
    if (digits.ends_with("usize"))
      digits.resize(digits.size() - 5);
    else if (digits.ends_with("i32") || digits.ends_with("u32") ||
             digits.ends_with("isize")) {
      report(span, "array length must have type usize");
      return std::nullopt;
    }
    std::uint64_t value{};
    if (!parse_unsigned(digits, value))
      report(span, "invalid array length");
    return value;
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
    if (constant.type_state != ConstantInfo::TypeState::Failed)
      constant.type_state = ConstantInfo::TypeState::Done;
  }

  static bool parse_unsigned(std::string text, std::uint64_t &value) {
    text.erase(std::remove(text.begin(), text.end(), '_'), text.end());
    int base = 10;
    std::size_t offset = 0;
    if (text.starts_with("0x")) {
      base = 16;
      offset = 2;
    } else if (text.starts_with("0o")) {
      base = 8;
      offset = 2;
    } else if (text.starts_with("0b")) {
      base = 2;
      offset = 2;
    }
    auto [end, error] = std::from_chars(text.data() + offset,
                                        text.data() + text.size(), value, base);
    return error == std::errc{} && end == text.data() + text.size();
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
      for (const auto &function_ast : implementation->functions) {
        record_symbol(function_ast.id, "associated-function",
                      owner + "::" + function_ast.name, owner_symbol);
        if (!associated_names.insert(function_ast.name).second)
          report(function_ast.span,
                 "duplicate associated item '" + function_ast.name + "'");
        auto key = owner + "::" + function_ast.name;
        if (associated_functions_.contains(key) ||
            associated_constants_.contains(key))
          report(function_ast.span,
                 "duplicate associated item '" + function_ast.name + "'");
        associated_functions_[key].ast = &function_ast;
        associated_functions_[key].owner = owner;
      }
      for (const auto &constant_ast : implementation->constants) {
        record_symbol(constant_ast.id, "associated-constant",
                      owner + "::" + constant_ast.name, owner_symbol);
        if (!associated_names.insert(constant_ast.name).second)
          report(constant_ast.span,
                 "duplicate associated item '" + constant_ast.name + "'");
        auto key = owner + "::" + constant_ast.name;
        if (associated_functions_.contains(key) ||
            associated_constants_.contains(key))
          report(constant_ast.span,
                 "duplicate associated item '" + constant_ast.name + "'");
        associated_constants_[key].ast = &constant_ast;
        associated_constants_[key].owner = owner;
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
      for (const auto &derive : structure.ast->derives) {
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
        record_symbol(field.id, "field", field.name, structure.ast->id);
        if (!fields.insert(field.name).second)
          report(field.span, "duplicate field '" + field.name + "'");
        structure.fields[field.name] = resolve_type(field.type, name);
        structure.field_symbols[field.name] = field.id;
      }
    }
    for (auto &[name, function] : functions_)
      resolve_function(function, name, {});
  }

  void resolve_function(FunctionInfo &function, const std::string &,
                        std::string owner) {
    function.owner = std::move(owner);
    std::unordered_set<std::string> parameters;
    for (const auto &parameter : function.ast->parameters) {
      if (!parameters.insert(parameter.name).second)
        report(parameter.span, "duplicate parameter '" + parameter.name + "'");
      function.parameters.push_back(
          resolve_type(parameter.type, function.owner));
    }
    function.result = resolve_type(function.ast->result, function.owner);
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

  std::pair<TyPtr, std::int64_t>
  constant_value(const ast::ExprPtr &expression, TyPtr expected = {},
                 std::string_view self_name = {}) {
    if (!expression)
      return {ty(Ty::Kind::Error), 0};
    if (expression->kind == ast::Expr::Kind::Boolean) {
      auto type = ty(Ty::Kind::Bool);
      record_expression(expression, ExprInfo{type});
      return {type, expression->text == "true"};
    }
    if (expression->kind == ast::Expr::Kind::Integer) {
      auto type = literal_type(expression->text, expected, expression->span);
      std::string digits = expression->text;
      for (auto suffix : {"usize", "isize", "u32", "i32"})
        if (digits.ends_with(suffix))
          digits.resize(digits.size() - std::string_view(suffix).size());
      std::uint64_t value{};
      parse_unsigned(digits, value);
      record_expression(expression, ExprInfo{type});
      return {type, static_cast<std::int64_t>(value)};
    }
    if (expression->kind == ast::Expr::Kind::Unary && expression->text == "-" &&
        !expression->operands.empty()) {
      auto [type, value] =
          constant_value(expression->operands.front(), expected, self_name);
      if (!signed_integer(type))
        report(expression->span, "only signed constants may be negated");
      record_expression(expression, ExprInfo{type});
      return {type, -value};
    }
    if (expression->kind == ast::Expr::Kind::Path) {
      auto name = join_path(expression->path);
      if (!self_name.empty() && name.starts_with("Self::"))
        name = std::string(self_name) + name.substr(4);
      if (auto it = constants_.find(name); it != constants_.end()) {
        evaluate_constant(it->second);
        record_expression(expression,
                          ExprInfo{it->second.type, false, false, false,
                                   it->second.ast->id, name});
        return {it->second.type, it->second.value};
      }
      if (auto it = associated_constants_.find(name);
          it != associated_constants_.end()) {
        evaluate_constant(it->second);
        record_expression(expression,
                          ExprInfo{it->second.type, false, false, false,
                                   it->second.ast->id, name});
        return {it->second.type, it->second.value};
      }
      report(expression->span, "constant path does not resolve to a constant");
      auto error = ty(Ty::Kind::Error);
      record_expression(expression, ExprInfo{error});
      return {error, 0};
    }
    report(expression->span, "invalid constant expression");
    auto error = ty(Ty::Kind::Error);
    record_expression(expression, ExprInfo{error});
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
    if (!main.ast->lifetime_parameters.empty())
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
    // Every function starts with isolated scopes and loop context.
    scopes_.clear();
    scopes_.emplace_back();
    current_function_ = &function;
    if (function.ast->has_self) {
      if (function.owner.empty())
        report(function.ast->span, "self is only available in a method");
      auto self = ty(Ty::Kind::Struct);
      self->name = function.owner;
      if (function.ast->self_by_ref)
        self = compound(Ty::Kind::Ref, self, function.ast->self_mutable);
      scopes_.back()["self"] = {self, function.ast->self_mutable,
                                function.ast->id};
    }
    for (std::size_t i = 0; i < function.ast->parameters.size(); ++i) {
      const auto &parameter = function.ast->parameters[i];
      if (protected_builtin_value(parameter.name))
        report(parameter.span, "cannot shadow a protected builtin");
      record_symbol(parameter.id, "parameter", parameter.name,
                    function.ast->id);
      scopes_.back()[parameter.name] = {function.parameters[i],
                                        parameter.is_mutable, parameter.id};
    }
    auto body = check_expr(function.ast->body, function.result);
    if (!coercible(body.type, function.result))
      mismatch(function.ast->body->span, body.type, function.result);
    current_function_ = nullptr;
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

  TyPtr literal_type(const std::string &text, const TyPtr &expected,
                     Span span) {
    TyPtr result;
    if (text.ends_with("usize"))
      result = ty(Ty::Kind::Usize);
    else if (text.ends_with("isize"))
      result = ty(Ty::Kind::Isize);
    else if (text.ends_with("u32"))
      result = ty(Ty::Kind::U32);
    else if (text.ends_with("i32"))
      result = ty(Ty::Kind::I32);
    else if (integer(expected))
      result = expected;
    else
      result = ty(Ty::Kind::I32);
    std::string digits = text;
    for (auto suffix : {"usize", "isize", "u32", "i32"})
      if (digits.ends_with(suffix))
        digits.resize(digits.size() - std::string_view(suffix).size());
    std::uint64_t value{};
    if (!parse_unsigned(digits, value) ||
        value > std::numeric_limits<std::uint32_t>::max())
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

  void record_expression(const ast::ExprPtr &expression,
                         const ExprInfo &result) {
    expression_facts_[expression->id] = {
        expression->id,    result.type,          result.is_place,
        result.is_mutable, result.access_locked, result.symbol,
        result.target,     result.adjustments};
  }

  ExprInfo check_expr(const ast::ExprPtr &expression, TyPtr expected = {}) {
    // Central dispatch lets contextual types flow into child expressions.
    if (!expression)
      return {};
    auto result = [&]() -> ExprInfo {
      switch (expression->kind) {
      case ast::Expr::Kind::Integer:
        return {literal_type(expression->text, expected, expression->span)};
      case ast::Expr::Kind::Boolean:
        return {ty(Ty::Kind::Bool)};
      case ast::Expr::Kind::Unit:
        return {ty(Ty::Kind::Unit)};
      case ast::Expr::Kind::Path:
        return check_path(expression);
      case ast::Expr::Kind::Array:
        return check_array(expression, expected);
      case ast::Expr::Kind::Struct:
        return check_struct(expression);
      case ast::Expr::Kind::Block:
        return check_block(expression, expected);
      case ast::Expr::Kind::If:
        return check_if(expression, expected);
      case ast::Expr::Kind::Loop:
        return check_loop(expression, expected);
      case ast::Expr::Kind::While:
        return check_while(expression);
      case ast::Expr::Kind::Unary:
        return check_unary(expression, expected);
      case ast::Expr::Kind::Binary:
        return check_binary(expression);
      case ast::Expr::Kind::Assign:
        return check_assignment(expression);
      case ast::Expr::Kind::Cast:
        return check_cast(expression);
      case ast::Expr::Kind::Call:
        return check_call(expression);
      case ast::Expr::Kind::MethodCall:
        return check_method(expression);
      case ast::Expr::Kind::Field:
        return check_field(expression);
      case ast::Expr::Kind::Index:
        return check_index(expression);
      case ast::Expr::Kind::Break:
        return check_break(expression, expected);
      case ast::Expr::Kind::Continue:
        return check_continue(expression);
      case ast::Expr::Kind::Return:
        return check_return(expression);
      }
      return {};
    }();
    if (expected && coercible(result.type, expected)) {
      auto adjustments = coercion_adjustments(result.type, expected);
      result.adjustments.insert(result.adjustments.end(), adjustments.begin(),
                                adjustments.end());
    }
    record_expression(expression, result);
    return result;
  }

  ExprInfo check_path(const ast::ExprPtr &expression) {
    auto name = join_path(expression->path);
    if (expression->path.size() > 1 && expression->path.front() == "Self" &&
        current_function_ && !current_function_->owner.empty()) {
      name = current_function_->owner;
      for (std::size_t i = 1; i < expression->path.size(); ++i)
        name += "::" + expression->path[i];
    }
    if (expression->path.size() == 1) {
      if (auto *binding = lookup(name))
        return {binding->type,   true, binding->is_mutable, false,
                binding->symbol, name};
      if (auto it = constants_.find(name); it != constants_.end())
        return {it->second.type, false, false, false, it->second.ast->id, name};
      if (auto it = functions_.find(name); it != functions_.end()) {
        auto value = ty(Ty::Kind::Function);
        value->name = name;
        return {value, false, false, false, it->second.ast->id, name};
      }
      if (builtin_function(name)) {
        auto value = ty(Ty::Kind::Function);
        value->name = name;
        return {value, false, false, false, std::nullopt, "builtin::" + name};
      }
      if (auto it = structures_.find(name); it != structures_.end()) {
        auto value = ty(Ty::Kind::Function);
        value->name = name;
        return {value, false, false, false, it->second.ast->id, name};
      }
    } else {
      if (auto it = associated_constants_.find(name);
          it != associated_constants_.end())
        return {it->second.type, false, false, false, it->second.ast->id, name};
      if (auto it = associated_functions_.find(name);
          it != associated_functions_.end()) {
        auto value = ty(Ty::Kind::Function);
        value->name = name;
        return {value, false, false, false, it->second.ast->id, name};
      }
      if (constructor_path(expression)) {
        auto value = ty(Ty::Kind::Function);
        value->name = name;
        std::optional<SymbolId> symbol;
        if (auto it = structures_.find(expression->path.front());
            it != structures_.end())
          symbol = it->second.ast->id;
        return {value, false, false, false, symbol, "builtin::" + name};
      }
    }
    report(expression->span, "unresolved value name '" + name + "'");
    return {};
  }

  static bool builtin_function(std::string_view name) {
    return protected_builtin_value(name) || name == "get_i32" ||
           name == "print_i32" || name == "println_i32";
  }

  bool constructor_path(const ast::ExprPtr &expression) {
    if (expression->path.size() != 2)
      return false;
    const bool builtin_container =
        expression->path[0] == "Box" || expression->path[0] == "Vec";
    const bool builtin_member =
        expression->path[1] == "new" || expression->path[1] == "clone" ||
        expression->path[1] == "len" || expression->path[1] == "is_empty" ||
        expression->path[1] == "push" || expression->path[1] == "remove";
    return (builtin_container && builtin_member) ||
           (structures_.contains(expression->path[0]) &&
            expression->path[1] == "clone");
  }

