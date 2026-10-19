#include "rx/llvm/bridge/semantic.hpp"
#include <stdexcept>

namespace rx::ir {
CallLowering SemanticBridge::lower_call(const ast::Expr &node,
                                        const std::vector<ast::ExprPtr> &arguments) const {
  const auto &fact = semantic_.expression(node);
  if (!fact.callable) {
    throw std::invalid_argument("call expression lacks a resolved target");
  }
  auto target = std::visit(ast::Overloaded{
                               [&](const FunctionTarget &value) -> decltype(CallLowering::target) {
                                 return FunctionCall{function(value.symbol)};
                               },
                               [](const BuiltinTarget &value) -> decltype(CallLowering::target) {
                                 return BuiltinCall{value.builtin};
                               },
                           },
                           *fact.callable);
  CallLowering result{std::move(target), fact.receiver, {}, fact.type};
  for (const auto &argument : arguments) {
    const auto &input = semantic_.expression(*argument);
    auto effective = input.type;
    if (!input.adjustments.empty()) {
      effective = input.adjustments.back().target;
    }
    result.arguments.push_back({argument->id, effective, input.adjustments});
  }
  return result;
}
CallLowering SemanticBridge::call(const ast::Expr &node, const ast::CallExpr &payload) const {
  if (ast::get_if<ast::CallExpr>(node) != &payload) {
    throw std::invalid_argument("call payload does not belong to this AST node");
  }
  return lower_call(node, payload.arguments);
}
CallLowering SemanticBridge::call(const ast::Expr &node, const ast::MethodCallExpr &payload) const {
  if (ast::get_if<ast::MethodCallExpr>(node) != &payload) {
    throw std::invalid_argument("method payload does not belong to this AST node");
  }
  return lower_call(node, payload.arguments);
}
}
