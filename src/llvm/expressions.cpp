#include "llvm/lowering.hpp"
#include "rx/semantic/model/literal.hpp"

namespace rx::ir {

Value FunctionLowering::intrinsic(const ast::Expr &node) {
  if (builder_.terminated())
    return unit(type(node));
  builder_.set_origin(node);
  return ast::visit(node, [&](const ast::Expr &owner, const auto &payload) {
    return emit(owner, payload);
  });
}

Value FunctionLowering::expression(const ast::Expr &node) {
  auto value = intrinsic(node);
  if (builder_.terminated())
    return value;
  value = adjust(std::move(value), facts().expression(node).adjustments);
  return freeze(std::move(value));
}

Value FunctionLowering::emit(const ast::Expr &node, const ast::IntegerExpr &payload) {
  auto bits = static_cast<std::uint32_t>(integer_magnitude(payload.literal).value());
  return {type(node), ScalarValue{Operand::constant(Type::I32, bits)}};
}

Value FunctionLowering::emit(const ast::Expr &node, const ast::BooleanExpr &payload) {
  return {type(node), ScalarValue{Operand::constant(Type::I1, payload.value)}};
}

Value FunctionLowering::emit(const ast::Expr &node, const ast::UnitExpr &) {
  return unit(type(node));
}

Value FunctionLowering::emit(const ast::Expr &node, const ast::PathExpr &) {
  const auto &fact = facts().expression(node);
  if (fact.type->kind() == SemanticType::Kind::BuiltinFunction)
    return {fact.type, ScalarValue{Operand::constant(Type::Ptr, 0)}};
  const auto symbol = fact.symbol.value();
  const auto kind = facts().symbol(symbol).kind;
  if (kind == SymbolKind::Function || kind == SymbolKind::AssociatedFunction)
    return {fact.type, ScalarValue{Operand::global(module_.function_name(symbol))}};
  if (kind == SymbolKind::Constant || kind == SymbolKind::AssociatedConstant) {
    const auto machine = module_.bridge.scalar_type(fact.type).value();
    return {fact.type, ScalarValue{Operand::constant(machine, facts().constant(symbol).bits)}};
  }
  return bindings_.at(symbol);
}

Value FunctionLowering::emit(const ast::Expr &, const ast::ParenExpr &payload) {
  return intrinsic(*payload.inner);
}

Value FunctionLowering::emit(const ast::Expr &, const ast::BlockExpr &payload) {
  return block(*payload.block);
}

Value FunctionLowering::block(const ast::Block &node) {
  for (const auto &statement : node.statements) {
    if (builder_.terminated())
      break;
    auto lower_statement = ast::Overloaded{
        [](const ast::Stmt &, const ast::EmptyStmt &) {},
        [&](const ast::Stmt &owner, const ast::LetStmt &payload) {
          auto value = expression(*payload.initializer);
          if (builder_.terminated())
            return;
          const auto symbol = facts().declaration(owner).id;
          bind(symbol, facts().symbol_type(symbol), std::move(value));
        },
        [&](const ast::Stmt &, const ast::ExprStmt &payload) {
          expression(*payload.expression);
        },
    };
    ast::visit(*statement, lower_statement);
  }
  Value result = unit(type(node));
  if (!builder_.terminated() && node.tail)
    result = expression(**node.tail);
  if (!builder_.terminated())
    result = adjust(std::move(result), facts().expression(node).adjustments);
  return result;
}

}
