#include "llvm/lowering.hpp"

namespace rx::ir {

Value FunctionLowering::emit(const ast::Expr &node, const ast::FieldExpr &payload) {
  auto base = intrinsic(*payload.base);
  if (builder_.terminated())
    return unit(type(node));
  const auto &fact = facts().expression(node);
  base = adjust(std::move(base), fact.receiver->adjustments);
  const auto &structure = module_.bridge.structure(base.type->symbol());
  for (const auto &field : structure.fields) {
    if (field.symbol == fact.symbol)
      return {type(node), StoredValue{offset(address(base), field.offset)}};
  }
  return unit(type(node));
}

Value FunctionLowering::emit(const ast::Expr &node, const ast::IndexExpr &payload) {
  auto base = intrinsic(*payload.base);
  if (builder_.terminated())
    return unit(type(node));
  if (base.type->kind() == SemanticType::Kind::Ref || base.type->kind() == SemanticType::Kind::Box)
    base = freeze(std::move(base));
  auto index = expression(*payload.index);
  if (builder_.terminated())
    return unit(type(node));
  base = adjust(std::move(base), facts().expression(node).receiver->adjustments);
  auto data = address(base);
  if (base.type->kind() == SemanticType::Kind::Vec)
    data = builder_.load(Type::Ptr, data);
  const auto stride = Operand::constant(Type::I32, module_.bridge.layout(type(node)).bytes);
  const auto bytes = builder_.binary(BinaryOp::Mul, scalar(index), stride);
  return {type(node), StoredValue{offset(data, bytes)}};
}

}
