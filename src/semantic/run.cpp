#include "detail/analyzer.hpp"

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

namespace rx::semantic_detail {

SemanticTypePtr error_type() {
  static const auto value = [] {
    TypeContext context;
    return context.primitive(SemanticType::Kind::Error);
  }();
  return value;
}

bool compatible_types(const SemanticTypePtr &lhs, const SemanticTypePtr &rhs) {

  return lhs && rhs &&
         (lhs->kind() == SemanticType::Kind::Error ||
          rhs->kind() == SemanticType::Kind::Error || same_type(lhs, rhs));
}

bool integer(const SemanticTypePtr &value) {
  return value && (value->kind() == SemanticType::Kind::I32 ||
                   value->kind() == SemanticType::Kind::U32 ||
                   value->kind() == SemanticType::Kind::Isize ||
                   value->kind() == SemanticType::Kind::Usize);
}

bool signed_integer(const SemanticTypePtr &value) {
  return value && (value->kind() == SemanticType::Kind::I32 ||
                   value->kind() == SemanticType::Kind::Isize);
}

Analyzer::Analyzer(const ast::Crate &crate, DiagnosticEngine &diagnostics)
    : crate_(crate), diagnostics_(diagnostics){}

SemanticResult Analyzer::run(){
  result_.source_crate = &crate_;

  collect_items();
  resolve_declarations();
  for (const auto &[_, function] : functions_)
    callable_functions_.emplace(symbol_for(function.ast->id), &function);
  for (const auto &[_, function] : associated_functions_)
    callable_functions_.emplace(symbol_for(function.ast->id), &function);
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
            [](const auto &left, const auto &right) {
              return left.symbol < right.symbol;
            });
  std::sort(result_.structures.begin(), result_.structures.end(),
            [](const auto &left, const auto &right) {
              return left.symbol < right.symbol;
            });
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
            [](const auto &left, const auto &right) {
              return left.symbol < right.symbol;
            });
  result_.success = !diagnostics_.has_errors();
  if (result_.success)
    return SemanticResult::publish(crate_, std::move(result_));
  return SemanticResult::rejected(crate_, std::move(result_));
}

std::string_view Analyzer::current_owner() const{
  if (current_function_)
    return current_function_->owner;
  return {};
}

SemanticTypePtr Analyzer::primitive_type(SemanticType::Kind kind){
  return types_.primitive(kind);
}

SemanticTypePtr Analyzer::compound(SemanticType::Kind kind, SemanticTypePtr element,
                         bool mutable_ref){
  switch (kind) {
  case SemanticType::Kind::Box:
    return types_.box(std::move(element));
  case SemanticType::Kind::Vec:
    return types_.vector(std::move(element));
  case SemanticType::Kind::Array:
    return types_.array(std::move(element), 0);
  case SemanticType::Kind::Ref: {
    auto mutability = ast::Mutability::Immutable;
    if (mutable_ref)
      mutability = ast::Mutability::Mutable;
    return types_.reference(std::move(element), mutability);
  }
  default:
    throw std::logic_error("invalid compound type factory");
  }
}

SemanticTypePtr Analyzer::structure_type(const std::string &name){
  if (auto found = structures_.find(name); found != structures_.end())
    return types_.structure(symbol_for(found->second.ast->id), name);
  return primitive_type(SemanticType::Kind::Error);
}

void Analyzer::report(Span span, std::string message){
  diagnostics_.error(span, std::move(message));
}

SemanticTypePtr Analyzer::resolve_type(const ast::TypePtr &syntax,
                             std::string_view self_name){
  auto result = resolve_type_impl(syntax, self_name);
  resolved_types_[syntax->id] = result;
  return result;
}

std::vector<const ast::TypePtr *>
Analyzer::type_arguments(const std::optional<ast::GenericArguments> &arguments){
  std::vector<const ast::TypePtr *> result;
  if (arguments)
    for (const auto &argument : arguments->values)
      if (auto *type = std::get_if<ast::TypePtr>(&argument))
        result.push_back(type);
  return result;
}

std::vector<const ast::TypePtr *>
Analyzer::type_arguments(const ast::Path &path){
  std::vector<const ast::TypePtr *> result;
  for (const auto &segment : path.segments) {
    auto arguments = type_arguments(segment.arguments);
    result.insert(result.end(), arguments.begin(), arguments.end());
  }
  return result;
}

SemanticTypePtr Analyzer::resolve_type(const std::optional<ast::TypePtr> &syntax,
                             std::string_view self_name){
  if (syntax)
    return resolve_type(*syntax, self_name);
  return primitive_type(SemanticType::Kind::Unit);
}

SemanticTypePtr Analyzer::resolve_type_impl(const ast::TypePtr &syntax,
                                  std::string_view self_name){
  return ast::visit(
      *syntax,
      ast::Overloaded{
          [&](const ast::TypeSyntax &, const ast::UnitType &) {
            return primitive_type(SemanticType::Kind::Unit);
          },
          [&](const ast::TypeSyntax &, const ast::ParenType &payload) {
            return resolve_type(payload.inner, self_name);
          },
          [&](const ast::TypeSyntax &, const ast::ReferenceType &payload) {
            return compound(SemanticType::Kind::Ref,
                            resolve_type(payload.referent, self_name),
                            payload.mutability == ast::Mutability::Mutable);
          },
          [&](const ast::TypeSyntax &, const ast::ArrayType &payload) {
            auto element = resolve_type(payload.element, self_name);
            auto length = evaluate_length(payload.length, self_name);
            return types_.array(std::move(element), length.value_or(0));
          },
          [&](const ast::TypeSyntax &node, const ast::PathType &payload) {
            return resolve_path_type(payload.path, node.span, self_name);
          }});
}

SemanticTypePtr Analyzer::resolve_path_type(const ast::Path &path, Span span,
                                  std::string_view self_name){
  const std::string name{ast::name_text(path.segments.back().name)};
  auto arguments = type_arguments(path);
  if (name == "Self") {
    if (self_name.empty()) {
      report(span, "Self is only available inside a struct or impl");
      return primitive_type(SemanticType::Kind::Error);
    }
    return structure_type(std::string(self_name));
  }
  static const std::unordered_map<std::string, SemanticType::Kind>
      primitives = {{"bool", SemanticType::Kind::Bool},
                    {"i32", SemanticType::Kind::I32},
                    {"u32", SemanticType::Kind::U32},
                    {"isize", SemanticType::Kind::Isize},
                    {"usize", SemanticType::Kind::Usize}};
  if (auto found = primitives.find(name); found != primitives.end()) {
    if (!arguments.empty())
      report(span, "primitive type '" + name + "' takes no type arguments");
    return primitive_type(found->second);
  }
  if (name == "Box" || name == "Vec") {
    if (arguments.size() != 1) {
      report(span, name + " requires exactly one concrete type argument");
      return primitive_type(SemanticType::Kind::Error);
    }
    auto element = resolve_type(*arguments.front(), self_name);
    if (name == "Box")
      return types_.box(std::move(element));
    return types_.vector(std::move(element));
  }
  if (structures_.find(name) != structures_.end()) {
    if (!arguments.empty())
      report(span, "struct '" + name + "' has no type parameters");
    return structure_type(name);
  }
  report(span, "unknown type '" + name + "'");
  return primitive_type(SemanticType::Kind::Error);
}

std::optional<std::uint64_t>
Analyzer::evaluate_length(const ast::ExprPtr &expression,
                std::string_view self_name){
  auto errors = diagnostics_.error_count();
  auto [type, value] = constant_value(
      expression, primitive_type(SemanticType::Kind::Usize), self_name);
  if (type->kind() != SemanticType::Kind::Usize)
    report(expression->span, "array length must have type usize");
  if (value < 0)
    report(expression->span, "array length cannot be negative");
  if (diagnostics_.error_count() != errors)
    return {};
  return static_cast<std::uint64_t>(value);
}

void Analyzer::resolve_constant_type(ConstantInfo &constant){
  if (constant.type_state == ConstantInfo::TypeState::Done ||
      constant.type_state == ConstantInfo::TypeState::Failed)
    return;
  if (constant.type_state == ConstantInfo::TypeState::Resolving) {
    report(constant.ast->span, "constant type dependency cycle");
    constant.type = primitive_type(SemanticType::Kind::Error);
    constant.type_state = ConstantInfo::TypeState::Failed;
    return;
  }
  constant.type_state = ConstantInfo::TypeState::Resolving;
  constant.type = resolve_type(constant.ast->type, constant.owner);
  record_symbol_type(constant.ast->id, constant.type);
  if (constant.type_state != ConstantInfo::TypeState::Failed)
    constant.type_state = ConstantInfo::TypeState::Done;
}

void Analyzer::resolve_declarations(){

  resolve_impls();

  for (auto &[_, constant] : constants_)
    resolve_constant_type(constant);
  for (auto &[_, constant] : associated_constants_)
    resolve_constant_type(constant);
  for (auto &[name, function] : associated_functions_)
    resolve_function(function, name, function.owner);
  resolve_structures();
  for (auto &[name, function] : functions_)
    resolve_function(function, name, {});
}

void Analyzer::resolve_function(FunctionInfo &function, const std::string &,
                      std::string owner){
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

void Analyzer::validate_constants(){
  for (auto &[_, constant] : constants_)
    evaluate_constant(constant);
  for (auto &[_, constant] : associated_constants_)
    evaluate_constant(constant);
}

void Analyzer::evaluate_constant(ConstantInfo &constant){

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
  if (!compatible_types(constant.type, value_type)) {
    report(constant.ast->span, "constant initializer has type " +
                                   type_name(value_type) + ", expected " +
                                   type_name(constant.type));
    constant.state = ConstantInfo::State::Failed;
    return;
  }
  constant.value = value;
  constant.state = ConstantInfo::State::Done;
}

Analyzer::EvaluatedConstant Analyzer::constant_path(const ast::Expr &node,
                                const ast::PathExpr &path,
                                std::string_view self_name){
  auto name = ast::path_name(path.path);
  if (!self_name.empty() && name.compare(0, 6, "Self::") == 0)
    name = std::string(self_name) + name.substr(4);
  if (auto found = constants_.find(name); found != constants_.end()) {
    evaluate_constant(found->second);
    record_node(node, ExprInfo{found->second.type, false, false, false,
                               symbol_for(found->second.ast->id), name});
    return {found->second.type, found->second.value};
  }
  if (auto found = associated_constants_.find(name);
      found != associated_constants_.end()) {
    evaluate_constant(found->second);
    record_node(node, ExprInfo{found->second.type, false, false, false,
                               symbol_for(found->second.ast->id), name});
    return {found->second.type, found->second.value};
  }
  report(node.span, "constant path does not resolve to a constant");
  auto error = primitive_type(SemanticType::Kind::Error);
  record_node(node, ExprInfo{error});
  return {error, 0};
}

Analyzer::EvaluatedConstant Analyzer::constant_value(const ast::ExprPtr &expression,
                                 SemanticTypePtr expected,
                                 std::string_view self_name){
  return ast::visit(
      *expression,
      ast::Overloaded{
          [&](const ast::Expr &node,
              const ast::ParenExpr &payload) -> EvaluatedConstant {
            auto value = constant_value(payload.inner, expected, self_name);
            record_node(node, ExprInfo{value.first});
            return value;
          },
          [&](const ast::Expr &node,
              const ast::BooleanExpr &payload) -> EvaluatedConstant {
            auto type = primitive_type(SemanticType::Kind::Bool);
            record_node(node, ExprInfo{type});
            return {type, payload.value};
          },
          [&](const ast::Expr &node,
              const ast::IntegerExpr &payload) -> EvaluatedConstant {
            auto type = literal_type(payload.literal, expected, node.span);
            auto magnitude = integer_magnitude(payload.literal).value_or(0);

            if (magnitude > std::numeric_limits<std::uint32_t>::max())
              magnitude = 0;
            record_node(node, ExprInfo{type});
            return {type, static_cast<std::int64_t>(magnitude)};
          },
          [&](const ast::Expr &node,
              const ast::UnaryExpr &payload) -> EvaluatedConstant {
            if (payload.op != ast::UnaryOp::Negate)
              return invalid_constant(node);
            auto [type, value] =
                constant_value(payload.operand, expected, self_name);
            if (!signed_integer(type))
              report(node.span, "only signed constants may be negated");
            ExprInfo result{type};
            result.unary = ResolvedUnary{
                UnaryOperation::Negate,
                OperandSemantics{payload.operand->id, type, {}}, type};
            record_node(node, result);
            return {type, -value};
          },
          [&](const ast::Expr &node,
              const ast::PathExpr &payload) -> EvaluatedConstant {
            return constant_path(node, payload, self_name);
          },
          [&](const ast::Expr &node, const auto &) -> EvaluatedConstant {
            return invalid_constant(node);
          }});
}

Analyzer::EvaluatedConstant Analyzer::invalid_constant(const ast::Expr &node){
  report(node.span, "invalid constant expression");
  auto error = primitive_type(SemanticType::Kind::Error);
  record_node(node, ExprInfo{error});
  return {error, 0};
}

void Analyzer::validate_layouts_and_derives(){
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

bool Analyzer::validate_layout(const std::string &name){

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
  std::function<void(const SemanticTypePtr &)> visit =
      [&](const SemanticTypePtr &field) {
        if (field->kind() == SemanticType::Kind::Struct)
          valid &= validate_layout(field->name());
        else if (field->kind() == SemanticType::Kind::Array)
          visit(field->element());
      };
  for (const auto &[_, field] : structure.fields)
    visit(field);
  structure.layout = StructInfo::LayoutState::Invalid;
  if (valid)
    structure.layout = StructInfo::LayoutState::Valid;
  return valid;
}

bool Analyzer::supports(const SemanticTypePtr &value, const std::string &trait,
              const std::string &root, std::unordered_set<std::string> seen){
  if (!value)
    return false;
  if (value->kind() == SemanticType::Kind::Bool || integer(value) ||
      value->kind() == SemanticType::Kind::Unit)
    return true;
  if (value->kind() == SemanticType::Kind::Ref) {
    if (trait == "Copy")
      return !value->is_mutable();
    if (trait == "Clone")
      return !value->is_mutable();
    if (trait == "PartialEq" || trait == "Eq")
      return supports(value->element(), trait, root, seen);
  }
  if (value->kind() == SemanticType::Kind::Box ||
      value->kind() == SemanticType::Kind::Vec ||
      value->kind() == SemanticType::Kind::Array) {
    if (trait == "Copy" && value->kind() != SemanticType::Kind::Array)
      return false;
    return supports(value->element(), trait, root, std::move(seen));
  }
  if (value->kind() == SemanticType::Kind::Struct) {
    if (seen.find(value->name()) != seen.end())
      return trait != "Copy";
    seen.insert(value->name());
    auto found = structures_.find(value->name());
    if (found == structures_.end() ||
        found->second.derives.find(trait) == found->second.derives.end())
      return false;
    for (const auto &[_, field] : found->second.fields)
      if (!supports(field, trait, root, seen))
        return false;
    return true;
  }
  return false;
}

void Analyzer::validate_entry(){
  auto found = functions_.find("main");
  if (found == functions_.end()) {
    report(crate_.span, "an executable requires a top-level main function");
    return;
  }
  const auto &main = found->second;
  if (!main.parameters.empty())
    report(main.ast->span, "main cannot have value parameters");
  if (main.ast->generics && !main.ast->generics->lifetimes.empty())
    report(main.ast->span, "main cannot have generic parameters");
  if (main.result->kind() != SemanticType::Kind::Unit)
    report(main.ast->span, "main must return unit");
}

void Analyzer::check_functions(){
  for (const auto &[_, function] : functions_)
    check_function(function);
  for (const auto &[_, function] : associated_functions_)
    check_function(function);
}

void Analyzer::check_function(const FunctionInfo &function){
  FunctionSemantics signature{
      symbol_for(function.ast->id), {}, {}, function.result};

  scopes_.clear();
  scopes_.emplace_back();
  current_function_ = &function;
  if (function.ast->signature.receiver.has_value()) {
    if (function.owner.empty())
      report(function.ast->span, "self is only available in a method");
    auto self = structure_type(function.owner);
    if (ast::by_reference(*function.ast->signature.receiver))
      self = compound(SemanticType::Kind::Ref, self,
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
  for (std::size_t index = 0;
       index < function.ast->signature.parameters.size(); ++index) {
    const auto &parameter = function.ast->signature.parameters[index];
    if (protected_builtin_value(parameter.binding.name.text))
      report(parameter.span, "cannot shadow a protected builtin");
    record_symbol(parameter.id, SymbolKind::Parameter,
                  parameter.binding.name.text, function.ast->id);
    record_symbol_type(parameter.id, function.parameters[index]);
    signature.parameters.push_back(
        {symbol_for(parameter.id), function.parameters[index]});
    scopes_.back()[parameter.binding.name.text] = {
        function.parameters[index],
        parameter.binding.mutability == ast::Mutability::Mutable,
        symbol_for(parameter.id)};
  }
  auto body = check_block(*function.ast->body, function.result);
  if (!coercible(body.type, function.result))
    mismatch(function.ast->body->span, body.type, function.result);
  current_function_ = nullptr;
  result_.functions.push_back(std::move(signature));
}

Binding *Analyzer::lookup(const std::string &name){
  for (auto scope = scopes_.rbegin(); scope != scopes_.rend(); ++scope)
    if (auto found = scope->find(name); found != scope->end())
      return &found->second;
  return nullptr;
}

bool Analyzer::coercible(const SemanticTypePtr &from, const SemanticTypePtr &to){
  if (!from || !to || from->kind() == SemanticType::Kind::Error ||
      to->kind() == SemanticType::Kind::Error ||
      from->kind() == SemanticType::Kind::Never)
    return true;
  if (compatible_types(from, to))
    return true;
  if (from->kind() == SemanticType::Kind::Ref &&
      to->kind() == SemanticType::Kind::Ref) {
    if (from->is_mutable() && !to->is_mutable() &&
        compatible_types(from->element(), to->element()))
      return true;
    auto current = from;
    while (current->kind() == SemanticType::Kind::Ref ||
           current->kind() == SemanticType::Kind::Box) {
      current = current->element();
      if (to->kind() == SemanticType::Kind::Ref &&
          (!to->is_mutable() || from->is_mutable()) &&
          compatible_types(current, to->element()))
        return true;
    }
  }
  return false;
}

SemanticTypePtr Analyzer::common_type(const SemanticTypePtr &first,
                            const SemanticTypePtr &second){
  if (coercible(second, first))
    return first;
  if (coercible(first, second))
    return second;
  return primitive_type(SemanticType::Kind::Error);
}

bool Analyzer::writable(const ExprInfo &value) const{
  if (value.access_locked)
    return false;
  auto current = value.type;
  while (current && current->kind() == SemanticType::Kind::Box)
    current = current->element();
  if (current && current->kind() == SemanticType::Kind::Ref)
    return current->is_mutable();
  return !value.is_place || value.is_mutable;
}

void Analyzer::mismatch(Span span, const SemanticTypePtr &actual,
              const SemanticTypePtr &expected){
  report(span, "type mismatch: found " + type_name(actual) + ", expected " +
                   type_name(expected));
}

SemanticTypePtr Analyzer::literal_type(const ast::IntegerLiteral &literal,
                             const SemanticTypePtr &expected, Span span){
  SemanticTypePtr result;
  if (literal.suffix) {
    switch (*literal.suffix) {
    case ast::IntegerSuffix::I32:
      result = primitive_type(SemanticType::Kind::I32);
      break;
    case ast::IntegerSuffix::U32:
      result = primitive_type(SemanticType::Kind::U32);
      break;
    case ast::IntegerSuffix::Isize:
      result = primitive_type(SemanticType::Kind::Isize);
      break;
    case ast::IntegerSuffix::Usize:
      result = primitive_type(SemanticType::Kind::Usize);
      break;
    }
  } else if (integer(expected))
    result = expected;
  else
    result = primitive_type(SemanticType::Kind::I32);
  auto value = integer_magnitude(literal);
  if (!value || *value > std::numeric_limits<std::uint32_t>::max())
    report(span, "integer literal is outside the 32-bit target range");
  return result;
}

std::vector<Adjustment>
Analyzer::coercion_adjustments(const SemanticTypePtr &from,
                     const SemanticTypePtr &to) const{
  std::vector<Adjustment> result;
  if (!from || !to || compatible_types(from, to) ||
      from->kind() == SemanticType::Kind::Error ||
      to->kind() == SemanticType::Kind::Error)
    return result;
  if (from->kind() == SemanticType::Kind::Never) {
    result.push_back({Adjustment::Kind::NeverToAny, to});
    return result;
  }
  if (from->kind() == SemanticType::Kind::Ref &&
      to->kind() == SemanticType::Kind::Ref && from->is_mutable() &&
      !to->is_mutable() && compatible_types(from->element(), to->element())) {
    result.push_back({Adjustment::Kind::MutToShared, to});
    return result;
  }
  if (from->kind() == SemanticType::Kind::Ref &&
      to->kind() == SemanticType::Kind::Ref) {
    auto current = from;
    while (current->kind() == SemanticType::Kind::Ref ||
           current->kind() == SemanticType::Kind::Box) {
      current = current->element();
      result.push_back({Adjustment::Kind::Dereference, current});
      if (compatible_types(current, to->element())) {
        auto borrow = Adjustment::Kind::BorrowShared;
        if (to->is_mutable())
          borrow = Adjustment::Kind::BorrowMutable;
        result.push_back({borrow, to});
        return result;
      }
    }
  }
  result.clear();
  return result;
}

void Analyzer::append_adjustments(ast::NodeId node,
                        std::vector<Adjustment> adjustments){
  auto &fact = expression_facts_[node];
  fact.node = node;
  fact.adjustments.insert(fact.adjustments.end(),
                          std::make_move_iterator(adjustments.begin()),
                          std::make_move_iterator(adjustments.end()));
}

void Analyzer::record_node(const ast::Node &node, const ExprInfo &result){
  ExpressionSemantics fact;
  fact.node = node.id;
  fact.type = result.type;
  fact.is_place = result.is_place;
  fact.is_mutable = result.is_mutable;
  fact.access_locked = result.access_locked;
  fact.symbol = result.symbol;
  fact.target = result.target;
  fact.adjustments = result.adjustments;
  fact.callable = result.callable;
  fact.binary = result.binary;
  fact.compound = result.compound;
  fact.unary = result.unary;
  fact.receiver = result.receiver;
  expression_facts_[node.id] = std::move(fact);
}

ExprInfo Analyzer::check_expr(const ast::ExprPtr &expression,
                    SemanticTypePtr expected){
  return check_expr(*expression, std::move(expected));
}

ExprInfo Analyzer::check_expr(const ast::Expr &expression,
                    SemanticTypePtr expected,
                    bool record_coercion){
  auto result = ast::visit(
      expression,
      ast::Overloaded{
          [&](const ast::Expr &node,
              const ast::IntegerExpr &payload) -> ExprInfo {
            return {literal_type(payload.literal, expected, node.span)};
          },
          [&](const ast::Expr &, const ast::BooleanExpr &) -> ExprInfo {
            return {primitive_type(SemanticType::Kind::Bool)};
          },
          [&](const ast::Expr &, const ast::UnitExpr &) -> ExprInfo {
            return {primitive_type(SemanticType::Kind::Unit)};
          },
          [&](const ast::Expr &, const ast::ParenExpr &payload) {
            auto inner = check_expr(*payload.inner, expected, false);
            inner.adjustments.clear();
            inner.binary.reset();
            inner.compound.reset();
            inner.unary.reset();
            inner.receiver.reset();
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
          [&](const ast::Expr &, const ast::BlockExpr &payload) {
            return check_block(*payload.block, expected);
          },
          [&](const ast::Expr &node, const ast::IfExpr &payload) {
            return check_if(node, payload, expected);
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
          [&](const ast::Expr &node, const ast::BreakExpr &payload) {
            return check_break(node, payload);
          },
          [&](const ast::Expr &node, const ast::ContinueExpr &payload) {
            return check_continue(node, payload);
          },
          [&](const ast::Expr &node, const ast::ReturnExpr &payload) {
            return check_return(node, payload);
          }});
  if (expected && record_coercion && coercible(result.type, expected)) {
    auto adjustments = coercion_adjustments(result.type, expected);
    result.adjustments.insert(result.adjustments.end(), adjustments.begin(),
                              adjustments.end());
  }
  record_node(expression, result);
  return result;
}

ExprInfo Analyzer::check_path(const ast::Expr &node, const ast::PathExpr &value){
  const auto &path = value.path;
  auto name = ast::path_name(path);
  if (!type_arguments(path).empty() && !constructor_path(path))
    report(node.span, "functions have no type parameters");
  if (path.segments.size() > 1 &&
      std::string(ast::name_text(path.segments.front().name)) == "Self" &&
      current_function_ && !current_function_->owner.empty()) {
    name = current_function_->owner;
    for (std::size_t index = 1; index < path.segments.size(); ++index)
      name += "::" + std::string(ast::name_text(path.segments[index].name));
  }
  if (path.segments.size() == 1) {
    if (auto *binding = lookup(name))
      return {binding->type,   true, binding->is_mutable, false,
              binding->symbol, name};
    if (auto found = constants_.find(name); found != constants_.end())
      return {found->second.type,
              false,
              false,
              false,
              symbol_for(found->second.ast->id),
              name};
    if (auto found = functions_.find(name); found != functions_.end()) {
      auto value = types_.function(symbol_for(found->second.ast->id), name);
      return {value, false, false, false, symbol_for(found->second.ast->id),
              name};
    }
    if (builtin_function(name)) {
      auto value = types_.builtin(io_builtin(name));
      return {value, false, false, false, std::nullopt, "builtin::" + name};
    }

  } else {
    if (auto found = associated_constants_.find(name);
        found != associated_constants_.end())
      return {found->second.type,
              false,
              false,
              false,
              symbol_for(found->second.ast->id),
              name};
    if (auto found = associated_functions_.find(name);
        found != associated_functions_.end()) {
      auto value = types_.function(symbol_for(found->second.ast->id), name);
      return {value, false, false, false, symbol_for(found->second.ast->id),
              name};
    }
    if (constructor_path(path)) {
      auto value = builtin_path_type(path, name);
      std::optional<SymbolId> symbol;
      if (auto found = structures_.find(
              std::string(ast::name_text(path.segments.front().name)));
          found != structures_.end())
        symbol = symbol_for(found->second.ast->id);
      return {value, false, false, false, symbol, "builtin::" + name};
    }
  }
  report(node.span, "unresolved value name '" + name + "'");
  return {};
}

BuiltinId Analyzer::io_builtin(std::string_view name){
  if (name == "getInt" || name == "get_i32")
    return BuiltinId::GetInt;
  if (name == "printInt" || name == "print_i32")
    return BuiltinId::PrintInt;
  return BuiltinId::PrintlnInt;
}

SemanticTypePtr Analyzer::builtin_path_type(const ast::Path &path,
                                  const std::string &name){
  const std::string owner{ast::name_text(path.segments[0].name)};
  const auto member = ast::name_text(path.segments[1].name);
  BuiltinId id = BuiltinId::Clone;
  if (member == "new") {
    id = BuiltinId::VecNew;
    if (owner == "Box")
      id = BuiltinId::BoxNew;
  } else if (member == "len")
    id = BuiltinId::Len;
  else if (member == "is_empty")
    id = BuiltinId::IsEmpty;
  else if (member == "push")
    id = BuiltinId::Push;
  else if (member == "remove")
    id = BuiltinId::Remove;
  if (structures_.find(owner) != structures_.end()) {
    auto type = structure_type(owner);
    if (!supports(type, "Clone", "", {}))
      report(path.span, "clone is not available for " + type_name(type));
    return types_.builtin(id, type);
  }
  auto arguments = type_arguments(path);
  if (arguments.size() != 1) {
    report(path.span, name + " requires one concrete type argument");
    return types_.builtin(id);
  }
  auto element = resolve_type(*arguments.front());
  SemanticTypePtr container;
  if (owner == "Box")
    container = types_.box(element);
  else
    container = types_.vector(element);
  return types_.builtin(id, std::move(container));
}

bool Analyzer::builtin_function(std::string_view name){
  return protected_builtin_value(name) || name == "get_i32" ||
         name == "print_i32" || name == "println_i32";
}

bool Analyzer::constructor_path(const ast::Path &path){
  if (path.segments.size() != 2)
    return false;
  const std::string first{ast::name_text(path.segments[0].name)};
  auto second = ast::name_text(path.segments[1].name);
  const bool member = second == "new" || second == "clone" ||
                      second == "len" || second == "is_empty" ||
                      second == "push" || second == "remove";
  const bool container_member =
      (first == "Vec" && member) ||
      (first == "Box" && (second == "new" || second == "clone"));
  return container_member ||
         ((structures_.find(first) != structures_.end()) &&
          second == "clone");
}

ExprInfo Analyzer::check_array(const ast::Expr &node, const ast::ArrayExpr &array,
                     const SemanticTypePtr &expected){
  SemanticTypePtr expected_element;
  if (expected && expected->kind() == SemanticType::Kind::Array)
    expected_element = expected->element();
  if (array.elements.empty()) {
    if (!expected_element) {
      report(node.span, "an empty array requires an expected array type");
      return {};
    }
    return {compound(SemanticType::Kind::Array, expected_element)};
  }
  auto first = check_expr(array.elements.front(), expected_element);

  for (std::size_t index = 1; index < array.elements.size(); ++index) {
    auto element = check_expr(array.elements[index], expected_element);
    auto merged = common_type(first.type, element.type);
    if (merged->kind() == SemanticType::Kind::Error)
      mismatch(array.elements[index]->span, element.type, first.type);
    else {
      if (!compatible_types(element.type, merged))
        append_adjustments(array.elements[index]->id,
                           coercion_adjustments(element.type, merged));
      first.type = merged;
    }
  }
  auto result = types_.array(first.type, array.elements.size());
  if (expected_element && (result->length() != expected->length() ||
                           !coercible(result, expected)))
    mismatch(node.span, result, expected);
  return {result};
}

ExprInfo Analyzer::check_repeat_array(const ast::Expr &node,
                            const ast::RepeatArrayExpr &array,
                            const SemanticTypePtr &expected){
  SemanticTypePtr expected_element;
  if (expected && expected->kind() == SemanticType::Kind::Array)
    expected_element = expected->element();
  auto first = check_expr(array.element, expected_element);
  check_expr(array.count, primitive_type(SemanticType::Kind::Usize));
  auto length = evaluate_length(array.count, current_owner());
  auto result = types_.array(first.type, length.value_or(0));
  if (result->length() > 1 && !supports(first.type, "Copy", "", {}))
    report(node.span, "array repetition requires Copy");
  if (expected_element && (result->length() != expected->length() ||
                           !coercible(result, expected)))
    mismatch(node.span, result, expected);
  return {result};
}

ExprInfo Analyzer::check_struct(const ast::Expr &node,
                      const ast::StructExpr &structure){
  auto name = ast::path_name(structure.path);
  if (name == "Self" && current_function_ &&
      !current_function_->owner.empty())
    name = current_function_->owner;
  auto found = structures_.find(name);
  if (found == structures_.end()) {
    report(node.span,
           "struct literal names an unknown struct '" + name + "'");
    return {};
  }
  std::unordered_set<std::string> initialized;
  for (const auto &field : structure.fields) {
    if (!initialized.insert(field.name.text).second)
      report(field.span,
             "duplicate field initializer '" + field.name.text + "'");
    auto expected = found->second.fields.find(field.name.text);
    if (expected == found->second.fields.end()) {
      report(field.span, "unknown field '" + field.name.text + "'");
      check_expr(field.value);
    } else {
      auto value = check_expr(field.value, expected->second);
      if (!coercible(value.type, expected->second))
        mismatch(field.value->span, value.type, expected->second);
    }
  }
  for (const auto &[field, _] : found->second.fields)
    if (initialized.find(field) == initialized.end())
      report(node.span, "missing field initializer '" + field + "'");
  auto result = structure_type(name);
  return {result, false, false, false, symbol_for(found->second.ast->id),
          name};
}

ExprInfo Analyzer::check_block(const ast::Block &block,
                     const SemanticTypePtr &expected){
  scopes_.emplace_back();
  SemanticTypePtr result = primitive_type(SemanticType::Kind::Unit);
  for (std::size_t index = 0; index < block.statements.size(); ++index) {
    const auto &statement = block.statements[index];
    ast::visit(
        *statement,
        ast::Overloaded{
            [&](const ast::Stmt &node, const ast::EmptyStmt &) {},
            [&](const ast::Stmt &node, const ast::LetStmt &payload) {
              SemanticTypePtr annotation;
              if (payload.annotation)
                annotation =
                    resolve_type(*payload.annotation, current_owner());
              auto initializer = check_expr(payload.initializer, annotation);
              auto binding_type = initializer.type;
              if (annotation)
                binding_type = annotation;
              if (annotation && !coercible(initializer.type, annotation))
                mismatch(payload.initializer->span, initializer.type,
                         annotation);
              if (annotation &&
                  annotation->kind() == SemanticType::Kind::Ref &&
                  annotation->is_mutable() && initializer.access_locked)
                report(payload.initializer->span,
                       "mutable reference initializer requires mutable "
                       "container access");
              std::optional<ast::NodeId> owner;
              if (current_function_)
                owner = current_function_->ast->id;
              record_symbol(node.id, SymbolKind::Local,
                            payload.binding.name.text, owner);
              record_symbol_type(node.id, binding_type);
              if (protected_builtin_value(payload.binding.name.text))
                report(payload.binding.name.span,
                       "cannot shadow a protected builtin");
              scopes_.back()[payload.binding.name.text] = {
                  binding_type,
                  payload.binding.mutability == ast::Mutability::Mutable,
                  symbol_for(node.id)};
            },
            [&](const ast::Stmt &node, const ast::ExprStmt &payload) {
              auto value = check_expr(payload.expression);
              if (!block.tail && index + 1 == block.statements.size() &&
                  value.type->kind() == SemanticType::Kind::Never)
                result = value.type;
              else if (payload.termination == ast::Termination::Omitted &&
                       value.type->kind() != SemanticType::Kind::Unit &&
                       value.type->kind() != SemanticType::Kind::Never)
                report(payload.expression->span,
                       "a non-final expression without a "
                       "semicolon must have unit type");
            }});
  }
  if (block.tail)
    result = check_expr(*block.tail, expected).type;
  scopes_.pop_back();
  record_node(block, ExprInfo{result});
  return {result};
}

ExprInfo Analyzer::check_if(const ast::Node &node, const ast::IfExpr &branch,
                  const SemanticTypePtr &expected){
  auto condition =
      check_expr(branch.condition, primitive_type(SemanticType::Kind::Bool));
  if (!compatible_types(condition.type,
                        primitive_type(SemanticType::Kind::Bool)))
    mismatch(branch.condition->span, condition.type,
             primitive_type(SemanticType::Kind::Bool));
  auto then_value = check_block(*branch.then_block, expected);
  if (!branch.else_branch) {
    if (then_value.type->kind() != SemanticType::Kind::Unit &&
        then_value.type->kind() != SemanticType::Kind::Never)
      report(node.span, "if without else requires a unit consequent");
    return {primitive_type(SemanticType::Kind::Unit)};
  }
  ast::NodeId else_id;
  auto else_value = std::visit(
      ast::Overloaded{[&](const ast::Box<ast::Block> &payload) {
                        else_id = payload->id;
                        return check_block(*payload, expected);
                      },
                      [&](const ast::Box<ast::IfBranch> &payload) {
                        else_id = payload->id;
                        auto result = ast::visit(
                            *payload, [&](const ast::IfBranch &owner,
                                          const ast::IfExpr &payload) {
                              return check_if(owner, payload, expected);
                            });
                        record_node(*payload, result);
                        return result;
                      }},
      *branch.else_branch);
  if (then_value.type->kind() == SemanticType::Kind::Never)
    return else_value;
  if (else_value.type->kind() == SemanticType::Kind::Never)
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

ExprInfo Analyzer::check_loop(const ast::Expr &node, const ast::LoopExpr &loop_expr,
                    const SemanticTypePtr &expected){
  loops_.push_back({expected, {}});
  auto body = check_block(*loop_expr.body);
  if (body.type->kind() != SemanticType::Kind::Unit &&
      body.type->kind() != SemanticType::Kind::Never)
    report(loop_expr.body->span, "loop body must have unit type");
  auto loop = std::move(loops_.back());
  loops_.pop_back();
  if (loop.break_types.empty())
    return {primitive_type(SemanticType::Kind::Never)};
  auto result = loop.break_types.front();
  if (loop.expected)
    result = loop.expected;
  for (const auto &value : loop.break_types)
    if (!coercible(value, result))
      mismatch(node.span, value, result);
  return {result};
}

ExprInfo Analyzer::check_while(const ast::Expr &node,
                     const ast::WhileExpr &while_expr){

  auto surrounding_loops = std::move(loops_);
  loops_.clear();
  auto condition = check_expr(while_expr.condition,
                              primitive_type(SemanticType::Kind::Bool));
  loops_ = std::move(surrounding_loops);
  if (!compatible_types(condition.type,
                        primitive_type(SemanticType::Kind::Bool)))
    mismatch(while_expr.condition->span, condition.type,
             primitive_type(SemanticType::Kind::Bool));
  loops_.push_back({primitive_type(SemanticType::Kind::Unit), {}});
  auto body =
      check_block(*while_expr.body, primitive_type(SemanticType::Kind::Unit));
  if (body.type->kind() != SemanticType::Kind::Unit &&
      body.type->kind() != SemanticType::Kind::Never)
    report(while_expr.body->span, "while body must have unit type");
  for (const auto &value : loops_.back().break_types)
    if (value->kind() != SemanticType::Kind::Unit)
      report(node.span, "break with a value is not allowed in while");
  loops_.pop_back();
  return {primitive_type(SemanticType::Kind::Unit)};
}

ExprInfo Analyzer::check_borrow(const ast::Expr &node, const ast::BorrowExpr &borrow){
  auto operand = check_expr(borrow.operand);
  bool mutable_ref = borrow.mutability == ast::Mutability::Mutable;
  if (mutable_ref &&
      ((operand.is_place && !operand.is_mutable) || operand.access_locked))
    report(borrow.operator_span, "mutable borrow requires a mutable place");
  return {compound(SemanticType::Kind::Ref, operand.type, mutable_ref)};
}

ExprInfo Analyzer::check_unary(const ast::Expr &node, const ast::UnaryExpr &unary,
                     const SemanticTypePtr &expected){
  SemanticTypePtr expected_operand = expected;
  if (unary.op == ast::UnaryOp::Dereference)
    expected_operand.reset();
  auto operand = check_expr(unary.operand, expected_operand);
  if (unary.op == ast::UnaryOp::Dereference) {
    if (operand.type->kind() != SemanticType::Kind::Ref &&
        operand.type->kind() != SemanticType::Kind::Box) {
      report(node.span,
             "type " + type_name(operand.type) + " cannot be dereferenced");
      return {};
    }
    const bool shared_layer =
        operand.type->kind() == SemanticType::Kind::Ref &&
        !operand.type->is_mutable();
    const bool locked = operand.access_locked || shared_layer;
    bool mutable_access = operand.is_mutable;
    if (operand.type->kind() == SemanticType::Kind::Ref)
      mutable_access = operand.type->is_mutable();
    mutable_access = mutable_access && !locked;
    ExprInfo result{operand.type->element(), true, mutable_access, locked};
    result.unary = ResolvedUnary{UnaryOperation::Dereference,
                                 operand_use(*unary.operand, operand, false),
                                 result.type};
    return result;
  }
  auto use = operand_use(*unary.operand, operand);
  if (unary.op == ast::UnaryOp::Not) {
    if (!integer(use.effective_type) &&
        use.effective_type->kind() != SemanticType::Kind::Bool)
      report(node.span, "operator ! requires bool or integer");
  } else if (!signed_integer(use.effective_type)) {
    report(node.span, "unary minus requires a signed integer");
  }
  ExprInfo result{use.effective_type};
  auto operation = UnaryOperation::Negate;
  if (unary.op == ast::UnaryOp::Not)
    operation = UnaryOperation::Complement;
  result.unary = ResolvedUnary{operation, std::move(use), result.type};
  return result;
}

OperandSemantics Analyzer::operand_use(const ast::Expr &node, const ExprInfo &info,
                             bool references, unsigned depth_limit,
                             bool mutable_references){
  OperandSemantics use{node.id, info.type, {}};

  for (const auto &adjustment : info.adjustments) {
    use.adjustments.push_back(adjustment);
    use.effective_type = adjustment.target;
  }
  for (unsigned depth = 0;
       references && depth < depth_limit &&
       use.effective_type->kind() == SemanticType::Kind::Ref &&
       (mutable_references || !use.effective_type->is_mutable());
       ++depth) {
    use.effective_type = use.effective_type->element();
    use.adjustments.push_back(
        {Adjustment::Kind::Dereference, use.effective_type});
  }
  return use;
}

ExprInfo Analyzer::check_binary(const ast::Expr &node, const ast::BinaryExpr &binary){
  auto left = check_expr(binary.left);
  auto right = check_expr(binary.right, left.type);
  const bool logical = binary.op == ast::BinaryOp::LogicalAnd ||
                       binary.op == ast::BinaryOp::LogicalOr;
  const bool ordering =
      binary.op == ast::BinaryOp::Lt || binary.op == ast::BinaryOp::Le ||
      binary.op == ast::BinaryOp::Gt || binary.op == ast::BinaryOp::Ge;
  unsigned depth = 1;
  if (ordering)
    depth = std::numeric_limits<unsigned>::max();
  auto lhs = operand_use(*binary.left, left, !logical, depth, ordering);
  auto rhs = operand_use(*binary.right, right, !logical, depth, ordering);
  if (ordering) {
    auto left_type = left.type;
    auto right_type = right.type;
    if ((left_type->kind() == SemanticType::Kind::Ref) !=
        (right_type->kind() == SemanticType::Kind::Ref))
      report(node.span, "ordering requires matching scalar operands");
    while (left_type->kind() == SemanticType::Kind::Ref &&
           right_type->kind() == SemanticType::Kind::Ref) {
      if (left_type->is_mutable() && !right_type->is_mutable())
        report(node.span, "ordering cannot coerce a mutable left reference "
                          "to a shared right reference");
      left_type = left_type->element();
      right_type = right_type->element();
    }
  }
  auto operation =
      resolve_operation(binary.op, lhs.effective_type, rhs.effective_type);
  if (!operation) {
    report(node.span, "operator " + std::string(ast::spelling(binary.op)) +
                          " is not defined for " +
                          type_name(lhs.effective_type) + " and " +
                          type_name(rhs.effective_type));
    return {};
  }
  if ((binary.op == ast::BinaryOp::Eq || binary.op == ast::BinaryOp::Ne) &&
      !supports(lhs.effective_type, "PartialEq", "", {}))
    report(node.span, "equality requires PartialEq");
  const bool boolean =
      std::holds_alternative<ScalarComparison>(*operation) ||
      std::holds_alternative<StructuralEquality>(*operation) || logical;
  auto result_type = lhs.effective_type;
  if (boolean)
    result_type = primitive_type(SemanticType::Kind::Bool);
  ExprInfo result{result_type};
  result.binary =
      ResolvedBinary{*operation, std::move(lhs), std::move(rhs), result.type};
  return result;
}

ExprInfo Analyzer::check_assignment(const ast::Expr &node,
                          const ast::AssignExpr &assignment){
  auto destination = check_expr(assignment.target);
  auto source = check_expr(assignment.value, destination.type);
  check_assignment_target(assignment.target, destination);
  if (!coercible(source.type, destination.type))
    mismatch(assignment.value->span, source.type, destination.type);
  return {primitive_type(SemanticType::Kind::Unit)};
}

void Analyzer::check_assignment_target(const ast::ExprPtr &target,
                             const ExprInfo &destination){
  if (!destination.is_place)
    report(target->span, "assignment destination is not a place");
  else if (!destination.is_mutable)
    report(target->span, "assignment destination is immutable");
}

ExprInfo
Analyzer::check_compound_assignment(const ast::Expr &node,
                          const ast::CompoundAssignExpr &assignment){
  auto destination = check_expr(assignment.target);
  auto source = check_expr(assignment.value, destination.type);
  check_assignment_target(assignment.target, destination);
  auto lhs = operand_use(*assignment.target, destination, false);
  auto rhs = operand_use(*assignment.value, source);
  auto operation = resolve_operation(binary_operation(assignment.op),
                                     lhs.effective_type, rhs.effective_type);
  if (!operation) {
    report(node.span,
           "compound operator " + std::string(ast::spelling(assignment.op)) +
               " is not defined for " + type_name(lhs.effective_type) +
               " and " + type_name(rhs.effective_type));
    return {primitive_type(SemanticType::Kind::Unit)};
  }
  ExprInfo result{primitive_type(SemanticType::Kind::Unit)};
  result.compound = ResolvedBinary{*operation, std::move(lhs), std::move(rhs),
                                   destination.type};
  return result;
}

ExprInfo Analyzer::check_cast(const ast::Expr &node, const ast::CastExpr &cast){
  auto source = check_expr(cast.value);
  auto destination = resolve_type(cast.target_type, current_owner());
  const bool valid = (integer(source.type) && integer(destination)) ||
                     (source.type->kind() == SemanticType::Kind::Bool &&
                      integer(destination));
  if (!valid)
    report(node.span, "unsupported cast from " + type_name(source.type) +
                          " to " + type_name(destination));
  return {destination};
}

ExprInfo Analyzer::check_call(const ast::Expr &node, const ast::CallExpr &call){
  auto callee = check_expr(call.callee);
  auto finish = [&](ExprInfo result) {
    result.callable = callee.callable;
    if (auto *function = rx::get_if<FunctionType>(*callee.type)) {
      result.symbol = function->symbol;
      result.target = function->name;
    } else if (auto *builtin =
                   rx::get_if<BuiltinFunctionType>(*callee.type)) {
      result.target =
          "builtin::" + std::string(builtin_name(builtin->builtin));
    }
    return result;
  };
  if (auto *function = rx::get_if<FunctionType>(*callee.type)) {
    const auto &info = *callable_functions_.at(function->symbol);
    auto parameters = info.parameters;
    if (info.ast->signature.receiver) {
      auto self = structure_type(info.owner);
      if (ast::by_reference(*info.ast->signature.receiver))
        self = compound(SemanticType::Kind::Ref, self,
                        ast::is_mutable(*info.ast->signature.receiver));
      parameters.insert(parameters.begin(), self);
    }
    return finish(
        check_signature(node, call.arguments, parameters, info.result));
  }
  if (auto *builtin = rx::get_if<BuiltinFunctionType>(*callee.type))
    return finish(check_builtin_call(node, call.arguments, *builtin));
  report(node.span, "called expression is not a function");
  for (const auto &argument : call.arguments)
    check_expr(argument);
  return {};
}

ExprInfo Analyzer::check_builtin_call(const ast::Expr &node,
                            const std::vector<ast::ExprPtr> &arguments,
                            const BuiltinFunctionType &builtin){
  switch (builtin.builtin) {
  case BuiltinId::GetInt:
    return check_signature(node, arguments, {},
                           primitive_type(SemanticType::Kind::I32));
  case BuiltinId::PrintInt:
  case BuiltinId::PrintlnInt:
    return check_signature(node, arguments,
                           {primitive_type(SemanticType::Kind::I32)},
                           primitive_type(SemanticType::Kind::Unit));
  default:
    break;
  }
  auto container = builtin.owner;
  if (!container) {
    report(node.span, "container function requires a resolved owner type");
    for (const auto &argument : arguments)
      check_expr(argument);
    return {};
  }
  switch (builtin.builtin) {
  case BuiltinId::BoxNew:
    return check_signature(node, arguments, {container->element()},
                           container);
  case BuiltinId::VecNew:
    return check_signature(node, arguments, {}, container);
  case BuiltinId::Clone:
    if (!supports(container, "Clone", "", {}))
      report(node.span, "clone is not available for " + type_name(container));
    return check_signature(node, arguments,
                           {compound(SemanticType::Kind::Ref, container)},
                           container);
  case BuiltinId::Push:
    return check_signature(
        node, arguments,
        {compound(SemanticType::Kind::Ref, container, true),
         container->element()},
        primitive_type(SemanticType::Kind::Unit));
  case BuiltinId::Remove:
    return check_signature(
        node, arguments,
        {compound(SemanticType::Kind::Ref, container, true),
         primitive_type(SemanticType::Kind::Usize)},
        container->element());
  case BuiltinId::Len:
    return check_signature(node, arguments,
                           {compound(SemanticType::Kind::Ref, container)},
                           primitive_type(SemanticType::Kind::Usize));
  case BuiltinId::IsEmpty:
    return check_signature(node, arguments,
                           {compound(SemanticType::Kind::Ref, container)},
                           primitive_type(SemanticType::Kind::Bool));
  case BuiltinId::GetInt:
  case BuiltinId::PrintInt:
  case BuiltinId::PrintlnInt:
    break;
  }
  throw std::logic_error("unhandled builtin call");
}

ExprInfo Analyzer::check_signature(const ast::Node &node,
                         const std::vector<ast::ExprPtr> &arguments,
                         const std::vector<SemanticTypePtr> &parameters,
                         SemanticTypePtr result){
  const auto count = arguments.size();
  if (count != parameters.size())
    report(node.span, "wrong number of function arguments");
  for (std::size_t index = 0; index < count; ++index) {
    SemanticTypePtr expected;
    if (index < parameters.size())
      expected = parameters[index];
    auto argument = check_expr(arguments[index], expected);
    if (expected && !coercible(argument.type, expected))
      mismatch(arguments[index]->span, argument.type, expected);
    if (expected && expected->kind() == SemanticType::Kind::Ref &&
        expected->is_mutable() && argument.access_locked)
      report(arguments[index]->span,
             "mutable reference argument requires mutable container access");
  }
  return {std::move(result)};
}

ExprInfo Analyzer::check_method(const ast::Expr &node,
                      const ast::MethodCallExpr &method){
  if (!type_arguments(method.generic_args).empty())
    report(node.span, "method segments have no type parameters");
  auto receiver = check_expr(method.receiver);
  SemanticTypePtr base = receiver.type;
  std::vector<Adjustment> receiver_adjustments;
  while (base->kind() == SemanticType::Kind::Ref ||
         base->kind() == SemanticType::Kind::Box) {
    base = base->element();
    receiver_adjustments.push_back({Adjustment::Kind::Dereference, base});
  }
  std::optional<BuiltinId> builtin;
  const auto &method_name = method.method.text;
  if (method_name == "clone")
    builtin = BuiltinId::Clone;
  else if (method_name == "len")
    builtin = BuiltinId::Len;
  else if (method_name == "is_empty")
    builtin = BuiltinId::IsEmpty;
  else if (method_name == "push")
    builtin = BuiltinId::Push;
  else if (method_name == "remove")
    builtin = BuiltinId::Remove;
  auto finish = [&](ExprInfo result, std::string target,
                    std::optional<SymbolId> symbol = {},
                    std::optional<Adjustment::Kind> borrow = {}) {
    result.target = std::move(target);
    result.symbol = symbol;
    if (symbol)
      result.callable = FunctionTarget{*symbol};
    else if (builtin)
      result.callable = BuiltinTarget{*builtin};
    result.receiver =
        OperandSemantics{method.receiver->id, base, receiver_adjustments};
    if (borrow) {
      const bool mutable_borrow = *borrow == Adjustment::Kind::BorrowMutable;
      auto borrowed = compound(SemanticType::Kind::Ref, base, mutable_borrow);
      result.receiver->adjustments.push_back({*borrow, borrowed});
      result.receiver->effective_type = borrowed;
    }
    return result;
  };
  const auto &name = method.method.text;
  if (name == "len" || name == "is_empty") {
    if (receiver.type->kind() != SemanticType::Kind::Vec &&
        receiver.type->kind() != SemanticType::Kind::Array &&
        !(receiver.type->kind() == SemanticType::Kind::Ref &&
          (receiver.type->element()->kind() == SemanticType::Kind::Vec ||
           receiver.type->element()->kind() == SemanticType::Kind::Array)))
      report(node.span, name + " is only available on arrays and Vec");
    auto result_type = primitive_type(SemanticType::Kind::Bool);
    if (name == "len")
      result_type = primitive_type(SemanticType::Kind::Usize);
    auto result =
        check_method_signature(node, method.arguments, {}, result_type);
    return finish(std::move(result), "builtin::" + name, {},
                  Adjustment::Kind::BorrowShared);
  }
  if (name == "push" || name == "remove") {
    SemanticTypePtr vector = receiver.type;
    if (vector->kind() == SemanticType::Kind::Ref)
      vector = vector->element();
    if (vector->kind() != SemanticType::Kind::Vec) {
      report(node.span, name + " is only available on Vec");
      return finish({}, "builtin::Vec::" + name);
    }
    if (!writable(receiver))
      report(node.span, name + " requires mutable access to Vec");
    if (name == "push")
      return finish(
          check_method_signature(node, method.arguments, {vector->element()},
                                 primitive_type(SemanticType::Kind::Unit)),
          "builtin::Vec::push", {}, Adjustment::Kind::BorrowMutable);
    return finish(
        check_method_signature(node, method.arguments,
                               {primitive_type(SemanticType::Kind::Usize)},
                               vector->element()),
        "builtin::Vec::remove", {}, Adjustment::Kind::BorrowMutable);
  }
  if (name == "clone") {
    auto cloned = receiver.type;
    if (cloned->kind() == SemanticType::Kind::Ref &&
        (cloned->element()->kind() == SemanticType::Kind::Box ||
         cloned->element()->kind() == SemanticType::Kind::Vec ||
         cloned->element()->kind() == SemanticType::Kind::Array))
      cloned = cloned->element();
    if (!supports(cloned, "Clone", "", {}))
      report(node.span, "clone is not available for " + type_name(cloned));

    base = receiver.type;
    receiver_adjustments.clear();
    while (!same_type(base, cloned)) {
      base = base->element();
      receiver_adjustments.push_back({Adjustment::Kind::Dereference, base});
    }
    return finish(check_method_signature(node, method.arguments, {}, cloned),
                  "builtin::clone", {}, Adjustment::Kind::BorrowShared);
  }
  if (base->kind() == SemanticType::Kind::Struct) {
    auto key = base->name() + "::" + name;
    if (auto found = associated_functions_.find(key);
        found != associated_functions_.end()) {
      const auto &function = found->second;
      if (!function.ast->signature.receiver.has_value()) {
        report(node.span, "associated function without self cannot be "
                          "called with dot syntax");
        return finish({}, key, symbol_for(function.ast->id));
      }
      if (ast::by_reference(*function.ast->signature.receiver) &&
          ast::is_mutable(*function.ast->signature.receiver) &&
          !writable(receiver))
        report(node.span, "method requires a mutable receiver");
      std::optional<Adjustment::Kind> borrow;
      const auto &declared_receiver = *function.ast->signature.receiver;
      if (ast::by_reference(declared_receiver)) {
        borrow = Adjustment::Kind::BorrowShared;
        if (ast::is_mutable(declared_receiver))
          borrow = Adjustment::Kind::BorrowMutable;
      }
      auto result = check_method_signature(
          node, method.arguments, function.parameters, function.result);
      return finish(std::move(result), key, symbol_for(function.ast->id),
                    borrow);
    }
  }
  report(node.span, "unknown method '" + name + "'");
  return finish({}, name);
}

ExprInfo Analyzer::check_method_signature(
    const ast::Node &node, const std::vector<ast::ExprPtr> &arguments,
    const std::vector<SemanticTypePtr> &parameters, SemanticTypePtr result){
  const auto count = arguments.size();
  if (count != parameters.size())
    report(node.span, "wrong number of method arguments");
  for (std::size_t index = 0; index < count; ++index) {
    SemanticTypePtr expected;
    if (index < parameters.size())
      expected = parameters[index];
    auto value = check_expr(arguments[index], expected);
    if (expected && !coercible(value.type, expected))
      mismatch(arguments[index]->span, value.type, expected);
  }
  return {std::move(result)};
}

ExprInfo Analyzer::check_field(const ast::Expr &node,
                     const ast::FieldExpr &projection){
  auto receiver = check_expr(projection.base);
  auto base = receiver.type;
  bool mutable_access = true;
  if (receiver.is_place)
    mutable_access = receiver.is_mutable;
  bool locked = receiver.access_locked;
  std::vector<Adjustment> adjustments;
  while (base->kind() == SemanticType::Kind::Ref ||
         base->kind() == SemanticType::Kind::Box) {
    if (base->kind() == SemanticType::Kind::Ref) {
      if (!base->is_mutable()) {
        locked = true;
        mutable_access = false;
      } else if (!locked) {

        mutable_access = true;
      }
    }
    base = base->element();
    adjustments.push_back({Adjustment::Kind::Dereference, base});
  }
  if (base->kind() != SemanticType::Kind::Struct ||
      structures_.find(base->name()) == structures_.end()) {
    report(node.span, "field access requires a struct");
    return {};
  }
  auto field =
      structures_.at(base->name()).fields.find(projection.field.text);
  if (field == structures_.at(base->name()).fields.end()) {
    report(node.span, "unknown field '" + projection.field.text + "'");
    return {};
  }
  ExprInfo result{
      field->second,
      true,
      mutable_access,
      locked,
      structures_.at(base->name()).field_symbols.at(projection.field.text),
      base->name() + "::" + projection.field.text};
  result.receiver =
      OperandSemantics{projection.base->id, base, std::move(adjustments)};
  return result;
}

ExprInfo Analyzer::check_index(const ast::Expr &node,
                     const ast::IndexExpr &projection){
  auto base = check_expr(projection.base);
  auto index =
      check_expr(projection.index, primitive_type(SemanticType::Kind::Usize));
  if (!compatible_types(index.type,
                        primitive_type(SemanticType::Kind::Usize)))
    mismatch(projection.index->span, index.type,
             primitive_type(SemanticType::Kind::Usize));
  auto container = base.type;
  bool mutable_access = true;
  if (base.is_place)
    mutable_access = base.is_mutable;
  bool locked = base.access_locked;
  std::vector<Adjustment> adjustments;
  while (container->kind() == SemanticType::Kind::Ref ||
         container->kind() == SemanticType::Kind::Box) {
    if (container->kind() == SemanticType::Kind::Ref) {
      if (!container->is_mutable()) {
        locked = true;
        mutable_access = false;
      } else if (!locked) {
        mutable_access = true;
      }
    }
    container = container->element();
    adjustments.push_back({Adjustment::Kind::Dereference, container});
  }
  if (container->kind() != SemanticType::Kind::Array &&
      container->kind() != SemanticType::Kind::Vec) {
    report(node.span, "type " + type_name(container) + " cannot be indexed");
    return {};
  }
  if (container->kind() == SemanticType::Kind::Vec && !mutable_access)
    locked = true;
  ExprInfo result{
      container->element(), true, mutable_access, locked, {}, "index"};
  result.receiver = OperandSemantics{projection.base->id, container,
                                     std::move(adjustments)};
  return result;
}

ExprInfo Analyzer::check_break(const ast::Expr &node, const ast::BreakExpr &payload){
  if (loops_.empty()) {
    report(node.span, "break is only available inside a loop");
    return {primitive_type(SemanticType::Kind::Never)};
  }
  ExprInfo value{primitive_type(SemanticType::Kind::Unit)};
  if (payload.value)
    value = check_expr(*payload.value, loops_.back().expected);
  loops_.back().break_types.push_back(value.type);
  return {primitive_type(SemanticType::Kind::Never)};
}

ExprInfo Analyzer::check_continue(const ast::Expr &node, const ast::ContinueExpr &){
  if (loops_.empty())
    report(node.span, "continue is only available inside a loop");
  return {primitive_type(SemanticType::Kind::Never)};
}

ExprInfo Analyzer::check_return(const ast::Expr &node, const ast::ReturnExpr &payload){
  auto expected = primitive_type(SemanticType::Kind::Error);
  if (current_function_)
    expected = current_function_->result;
  ExprInfo value{primitive_type(SemanticType::Kind::Unit)};
  if (payload.value)
    value = check_expr(*payload.value, expected);
  if (!coercible(value.type, expected))
    mismatch(node.span, value.type, expected);
  return {primitive_type(SemanticType::Kind::Never)};
}

void Analyzer::resolve_impls() {
  for (const auto *implementation : impls_) {
    auto target = resolve_type(implementation->target);
    if (target->kind() != SemanticType::Kind::Struct ||
        structures_.find(target->name()) == structures_.end()) {
      report(implementation->span,
             "an inherent impl target must be a user-defined struct");
      continue;
    }
    const std::string owner = target->name();
    const auto owner_symbol = structures_.at(owner).ast->id;
    std::unordered_set<std::string> associated_names;
    for (const auto &member : implementation->members) {
      ast::visit(member, [&](const ast::Node &, const auto &declaration) {
        using Payload = std::decay_t<decltype(declaration)>;
        const auto &name = declaration.name.text;
        auto key = owner + "::" + name;
        auto kind = SymbolKind::AssociatedConstant;
        if constexpr (std::is_same_v<Payload, ast::Function>)
          kind = SymbolKind::AssociatedFunction;
        record_symbol(declaration.id, kind, key, owner_symbol);
        if (!associated_names.insert(name).second ||
            (associated_functions_.find(key) !=
             associated_functions_.end()) ||
            (associated_constants_.find(key) != associated_constants_.end()))
          report(declaration.span,
                 "duplicate associated item '" + name + "'");
        if constexpr (std::is_same_v<Payload, ast::Function>) {
          associated_functions_[key].ast = &declaration;
          associated_functions_[key].owner = owner;
        } else {
          static_assert(std::is_same_v<Payload, ast::Constant>);
          associated_constants_[key].ast = &declaration;
          associated_constants_[key].owner = owner;
        }
      });
    }
  }
}

void Analyzer::resolve_structures() {
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
    if ((structure.derives.find("Copy") != structure.derives.end()) &&
        structure.derives.find("Clone") == structure.derives.end())
      report(structure.ast->span, "Copy requires an explicit Clone derive");
    if ((structure.derives.find("Eq") != structure.derives.end()) &&
        structure.derives.find("PartialEq") == structure.derives.end())
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
}

} // namespace rx::semantic_detail

namespace rx {

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
  return semantic_detail::Analyzer(crate, diagnostics).run();
}

std::string dump_semantic(const SemanticResult &checked) {
  const auto &result = checked.data();
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
    out << "    #" << expression.node << " type=" << type_name(expression.type);
    if (expression.is_place) {
      out << " place";
      if (expression.is_mutable)
        out << " mutable";
      else
        out << " immutable";
    } else {
      out << " value";
    }
    if (expression.access_locked)
      out << " access-locked";
    if (expression.symbol)
      out << " symbol=#" << *expression.symbol;
    if (!expression.target.empty())
      out << " target=" << expression.target;
    if (!expression.adjustments.empty()) {
      out << " adjustments=[";
      for (std::size_t index = 0; index < expression.adjustments.size();
           ++index) {
        if (index)
          out << ", ";
        const auto &adjustment = expression.adjustments[index];
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
