#include "detail/analyzer.hpp"

namespace rx::semantic_detail {

BuiltinId Analyzer::io_builtin(std::string_view name) {
  if (name == "getInt" || name == "get_i32")
    return BuiltinId::GetInt;
  if (name == "printInt" || name == "print_i32")
    return BuiltinId::PrintInt;
  return BuiltinId::PrintlnInt;
}

SemanticTypePtr Analyzer::builtin_path_type(const ast::Path &path,
                                            const std::string &name) {
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

bool Analyzer::builtin_function(std::string_view name) {
  return protected_builtin_value(name) || name == "get_i32" ||
         name == "print_i32" || name == "println_i32";
}

bool Analyzer::constructor_path(const ast::Path &path) {
  if (path.segments.size() != 2)
    return false;
  const std::string first{ast::name_text(path.segments[0].name)};
  auto second = ast::name_text(path.segments[1].name);
  const bool member = second == "new" || second == "clone" || second == "len" ||
                      second == "is_empty" || second == "push" ||
                      second == "remove";
  const bool container_member =
      (first == "Vec" && member) ||
      (first == "Box" && (second == "new" || second == "clone"));
  return container_member ||
         ((structures_.find(first) != structures_.end()) && second == "clone");
}

ExprInfo Analyzer::check_call(const ast::Expr &node,
                              const ast::CallExpr &call) {
  auto callee = check_expr(call.callee);
  auto finish = [&](ExprInfo result) {
    result.callable = callee.callable;
    if (auto *function = rx::get_if<FunctionType>(*callee.type)) {
      result.symbol = function->symbol;
      result.target = function->name;
    } else if (auto *builtin = rx::get_if<BuiltinFunctionType>(*callee.type)) {
      result.target = "builtin::" + std::string(builtin_name(builtin->builtin));
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

ExprInfo
Analyzer::check_builtin_call(const ast::Expr &node,
                             const std::vector<ast::ExprPtr> &arguments,
                             const BuiltinFunctionType &builtin) {
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
    return check_signature(node, arguments, {container->element()}, container);
  case BuiltinId::VecNew:
    return check_signature(node, arguments, {}, container);
  case BuiltinId::Clone:
    if (!supports(container, "Clone", "", {}))
      report(node.span, "clone is not available for " + type_name(container));
    return check_signature(node, arguments,
                           {compound(SemanticType::Kind::Ref, container)},
                           container);
  case BuiltinId::Push:
    return check_signature(node, arguments,
                           {compound(SemanticType::Kind::Ref, container, true),
                            container->element()},
                           primitive_type(SemanticType::Kind::Unit));
  case BuiltinId::Remove:
    return check_signature(node, arguments,
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

ExprInfo Analyzer::check_signature(
    const ast::Node &node, const std::vector<ast::ExprPtr> &arguments,
    const std::vector<SemanticTypePtr> &parameters, SemanticTypePtr result) {
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

} // namespace rx::semantic_detail
