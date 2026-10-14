#include "detail/analyzer.hpp"

namespace rx::semantic_detail {

ExprInfo Analyzer::check_method(const ast::Expr &node,
                                const ast::MethodCallExpr &method) {
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
    const std::vector<SemanticTypePtr> &parameters, SemanticTypePtr result) {
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

} // namespace rx::semantic_detail
