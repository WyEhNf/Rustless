#include "llvm/lowering.hpp"

namespace rx::ir {

Value FunctionLowering::emit(const ast::Expr &node, const ast::AssignExpr &payload) {
  auto source = expression(*payload.value);
  if (builder_.terminated())
    return unit(type(node));
  auto destination = intrinsic(*payload.target);
  if (builder_.terminated())
    return unit(type(node));
  source = convert(std::move(source), destination.type);
  write(destination.type, address(destination), std::move(source));
  return unit(type(node));
}

Value FunctionLowering::emit(const ast::Expr &node, const ast::CompoundAssignExpr &payload) {
  const auto operation = module_.bridge.compound(node, payload);
  auto source = intrinsic(*payload.value);
  if (builder_.terminated())
    return unit(type(node));
  source = freeze(std::move(source));
  auto destination = intrinsic(*payload.target);
  if (builder_.terminated())
    return unit(type(node));
  const auto output = address(destination);
  const auto machine = module_.bridge.scalar_type(destination.type).value();
  BinaryLowering binary{operation.operation, operation.target, operation.value, machine};
  auto result = apply_binary(binary, freeze(destination), std::move(source), destination.type);
  write(destination.type, output, std::move(result));
  return unit(type(node));
}

}
