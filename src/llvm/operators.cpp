#include "lowering.hpp"

namespace rx::ir {

Value FunctionLowering::apply_binary(const BinaryLowering &operation, Value left, Value right) {
  left = adjust(std::move(left), operation.left.adjustments);
  right = adjust(std::move(right), operation.right.adjustments);
  auto lower_operation = ast::Overloaded{
      [&](const ScalarArithmetic &arithmetic) {
        auto lhs = scalar(left);
        auto rhs = scalar(right);
        if (arithmetic.shift_count_mask) {
          const auto mask = Operand::constant(Type::I32, *arithmetic.shift_count_mask);
          rhs = builder_.binary(BinaryOp::And, rhs, mask);
        }
        return builder_.binary(arithmetic.op, lhs, rhs);
      },
      [&](const ScalarComparison &comparison) {
        return builder_.compare(comparison.predicate, scalar(left), scalar(right));
      },
      [&](const StructuralEquality &equality) {
        const auto name = module_.helper(HelperKind::Equal, equality.compared_type);
        auto result = builder_.call(Type::I1, name, {address(left), address(right)}).value();
        if (equality.negated)
          result = builder_.binary(BinaryOp::Xor, result, Operand::constant(Type::I1, 1));
        return result;
      },
      [&](ShortCircuit logical) {
        BinaryOp operation = BinaryOp::And;
        if (logical == ShortCircuit::Or)
          operation = BinaryOp::Or;
        return builder_.binary(operation, scalar(left), scalar(right));
      },
  };
  const auto result = std::visit(lower_operation, operation.operation);
  return {left.type, ScalarValue{result}};
}

Value FunctionLowering::emit(const ast::Expr &node, const ast::BinaryExpr &payload) {
  const auto operation = module_.bridge.binary(node, payload);
  if (const auto *logical = std::get_if<ShortCircuit>(&operation.operation))
    return short_circuit(node, payload, *logical);
  auto left = freeze(intrinsic(*payload.left));
  if (builder_.terminated())
    return unit(type(node));
  auto right = intrinsic(*payload.right);
  if (builder_.terminated())
    return unit(type(node));
  auto result = apply_binary(operation, std::move(left), freeze(std::move(right)));
  result.type = type(node);
  return result;
}

Value FunctionLowering::emit(const ast::Expr &node, const ast::UnaryExpr &payload) {
  const auto operation = module_.bridge.unary(node, payload);
  auto operand = intrinsic(*payload.operand);
  if (builder_.terminated())
    return unit(type(node));
  operand = adjust(std::move(operand), operation.operand.adjustments);
  if (std::holds_alternative<DereferencePlace>(operation.operation))
    return {type(node), StoredValue{scalar(std::move(operand))}};
  const auto arithmetic = std::get<ScalarUnary>(operation.operation);
  auto constant = Operand::constant(arithmetic.operand_type, arithmetic.left_constant);
  auto result = builder_.binary(arithmetic.op, constant, scalar(std::move(operand)));
  return {type(node), ScalarValue{result}};
}

}
