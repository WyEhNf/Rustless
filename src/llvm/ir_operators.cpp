#include "rx/ir_semantic.hpp"
#include <stdexcept>

namespace rx::ir {
namespace {
bool signed_integer(const SemanticTypePtr &type) {
  if (!type)
    throw std::invalid_argument("missing resolved operand type");
  return type->kind() == SemanticType::Kind::I32 ||
         type->kind() == SemanticType::Kind::Isize;
}
BinaryOp arithmetic(ArithmeticOp op, bool sign) {
  switch (op) {
  case ArithmeticOp::Add: return BinaryOp::Add;
  case ArithmeticOp::Sub: return BinaryOp::Sub;
  case ArithmeticOp::Mul: return BinaryOp::Mul;
  case ArithmeticOp::Div: return sign ? BinaryOp::SDiv : BinaryOp::UDiv;
  case ArithmeticOp::Rem: return sign ? BinaryOp::SRem : BinaryOp::URem;
  }
  throw std::invalid_argument("invalid resolved arithmetic operation");
}
BinaryOp bitwise(BitwiseOp op) {
  switch (op) {
  case BitwiseOp::And: return BinaryOp::And;
  case BitwiseOp::Or: return BinaryOp::Or;
  case BitwiseOp::Xor: return BinaryOp::Xor;
  }
  throw std::invalid_argument("invalid resolved bitwise operation");
}
Predicate comparison(Relation relation, bool sign) {
  switch (relation) {
  case Relation::Eq: return Predicate::Eq;
  case Relation::Ne: return Predicate::Ne;
  case Relation::Lt: return sign ? Predicate::SLt : Predicate::ULt;
  case Relation::Le: return sign ? Predicate::SLe : Predicate::ULe;
  case Relation::Gt: return sign ? Predicate::SGt : Predicate::UGt;
  case Relation::Ge: return sign ? Predicate::SGe : Predicate::UGe;
  }
  throw std::invalid_argument("invalid resolved comparison");
}
ShortCircuit short_circuit(LogicalOp op) {
  switch (op) {
  case LogicalOp::And: return ShortCircuit::And;
  case LogicalOp::Or: return ShortCircuit::Or;
  }
  throw std::invalid_argument("invalid resolved logical operation");
}
} // namespace
BinaryLowering
SemanticBridge::lower_binary(const ResolvedBinary &resolved) const {
  const auto operand = scalar_type(resolved.left.effective_type);
  const auto result = scalar_type(resolved.result);
  if (!result || *result == Type::Void)
    throw std::invalid_argument("resolved operation has no scalar result");
  const bool sign = signed_integer(resolved.left.effective_type);
  auto require_scalar = [&](bool integer_only = false) {
    if (!operand || *operand == Type::Void ||
        (integer_only && *operand != Type::I32))
      throw std::invalid_argument("resolved scalar operand type is invalid");
    return *operand;
  };
  auto operation = std::visit(
      ast::Overloaded{
          [&](const IntegerArithmetic &p) -> LoweredOperation {
            return ScalarArithmetic{arithmetic(p.op, sign), require_scalar(true),
                                    {}};
          },
          [&](const BitwiseOperation &p) -> LoweredOperation {
            const auto type = require_scalar();
            if (type != Type::I1 && type != Type::I32)
              throw std::invalid_argument("bitwise operands must be integers");
            return ScalarArithmetic{bitwise(p.op), type, {}};
          },
          [&](const ShiftOperation &p) -> LoweredOperation {
            if (p.count_policy != ShiftCountPolicy::MaskToWidth)
              throw std::invalid_argument("unsupported shift count policy");
            BinaryOp op;
            switch (p.direction) {
            case ShiftDirection::Left: op = BinaryOp::Shl; break;
            case ShiftDirection::Right:
              op = sign ? BinaryOp::AShr : BinaryOp::LShr;
              break;
            default: throw std::invalid_argument("invalid shift direction");
            }
            return ScalarArithmetic{op, require_scalar(true),
                                    target_.shift_count_mask()};
          },
          [&](const rx::ScalarComparison &p) -> LoweredOperation {
            return ScalarComparison{comparison(p.relation, sign),
                                    require_scalar()};
          },
          [&](const rx::StructuralEquality &p) -> LoweredOperation {
            switch (p.op) {
            case EqualityOp::Eq:
              return StructuralEquality{resolved.left.effective_type, false};
            case EqualityOp::Ne:
              return StructuralEquality{resolved.left.effective_type, true};
            }
            throw std::invalid_argument("invalid structural equality");
          },
          [&](const LogicalOperation &p) -> LoweredOperation {
            if (require_scalar() != Type::I1)
              throw std::invalid_argument("logical operands must be bool");
            return short_circuit(p.op);
          }},
      resolved.operation);
  return {std::move(operation), resolved.left, resolved.right, *result};
}
} // namespace rx::ir
