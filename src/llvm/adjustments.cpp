#include "lowering.hpp"

namespace rx::ir {

Value FunctionLowering::dereference(Value value) {
  const auto element = value.type->element();
  return {element, StoredValue{scalar(std::move(value))}};
}

Value FunctionLowering::adjust(Value value, const std::vector<Adjustment> &adjustments) {
  for (const auto &adjustment : adjustments) {
    switch (adjustment.kind) {
    case Adjustment::Kind::Dereference:
      value = dereference(std::move(value));
      break;
    case Adjustment::Kind::BorrowShared:
    case Adjustment::Kind::BorrowMutable:
      value = {adjustment.target, ScalarValue{address(std::move(value))}};
      break;
    case Adjustment::Kind::MutToShared:
    case Adjustment::Kind::NeverToAny:
      break;
    }
    value.type = adjustment.target;
  }
  return value;
}

Value FunctionLowering::convert(Value value, const SemanticTypePtr &target) {
  if (same_type(value.type, target))
    return value;
  if (value.type->kind() == SemanticType::Kind::Ref &&
      target->kind() == SemanticType::Kind::Ref) {
    if (!same_type(value.type->element(), target->element())) {
      while (!same_type(value.type, target->element()))
        value = dereference(std::move(value));
      return {target, ScalarValue{address(std::move(value))}};
    }
  }
  value.type = target;
  return value;
}

Value FunctionLowering::emit(const ast::Expr &node, const ast::BorrowExpr &payload) {
  auto operand = intrinsic(*payload.operand);
  if (builder_.terminated())
    return unit(type(node));
  return {type(node), ScalarValue{address(std::move(operand))}};
}

Value FunctionLowering::emit(const ast::Expr &node, const ast::CastExpr &payload) {
  auto value = expression(*payload.value);
  if (builder_.terminated())
    return unit(type(node));
  auto operand = scalar(std::move(value));
  const auto target = module_.bridge.scalar_type(type(node)).value();
  if (target == operand.type)
    return {type(node), ScalarValue{operand}};
  if (target == Type::I1) {
    auto zero = Operand::constant(operand.type, 0);
    return {type(node), ScalarValue{builder_.compare(Predicate::Ne, operand, zero)}};
  }
  CastOp operation = CastOp::ZExt;
  if (target == Type::Ptr)
    operation = CastOp::IntToPtr;
  else if (operand.type == Type::Ptr)
    operation = CastOp::PtrToInt;
  return {type(node), ScalarValue{builder_.cast(operation, target, operand)}};
}

}
