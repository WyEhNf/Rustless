#include "rx/semantic/model/operation.hpp"
#include <stdexcept>

namespace rx {
namespace {
bool integer(const SemanticTypePtr &type) {
  using TypeKind = SemanticType::Kind;
  return type &&
         (type->kind() == TypeKind::I32 || type->kind() == TypeKind::U32 ||
          type->kind() == TypeKind::Isize || type->kind() == TypeKind::Usize);
}
} // namespace
std::optional<ResolvedOperation>
resolve_operation(ast::BinaryOp op, const SemanticTypePtr &left,
                  const SemanticTypePtr &right) {
  using BinaryOp = ast::BinaryOp;
  using TypeKind = SemanticType::Kind;
  if (!left || !right)
    return {};
  const bool equal = same_type(left, right);
  const bool integers = integer(left) && integer(right);
  const bool booleans =
      left->kind() == TypeKind::Bool && right->kind() == TypeKind::Bool;
  switch (op) {
  case BinaryOp::Add:
  case BinaryOp::Sub:
  case BinaryOp::Mul:
  case BinaryOp::Div:
  case BinaryOp::Rem:
    if (!integers || !equal)
      return {};
    switch (op) {
    case BinaryOp::Add:
      return IntegerArithmetic{ArithmeticOp::Add};
    case BinaryOp::Sub:
      return IntegerArithmetic{ArithmeticOp::Sub};
    case BinaryOp::Mul:
      return IntegerArithmetic{ArithmeticOp::Mul};
    case BinaryOp::Div:
      return IntegerArithmetic{ArithmeticOp::Div};
    default:
      return IntegerArithmetic{ArithmeticOp::Rem};
    }
  case BinaryOp::BitAnd:
  case BinaryOp::BitOr:
  case BinaryOp::BitXor:
    if (!equal || (!integers && !booleans))
      return {};
    if (op == BinaryOp::BitAnd)
      return BitwiseOperation{BitwiseOp::And};
    if (op == BinaryOp::BitOr)
      return BitwiseOperation{BitwiseOp::Or};
    return BitwiseOperation{BitwiseOp::Xor};
  case BinaryOp::Shl:
  case BinaryOp::Shr:
    if (!integers)
      return {};
    if (op == BinaryOp::Shl)
      return ShiftOperation{ShiftDirection::Left};
    return ShiftOperation{ShiftDirection::Right};
  case BinaryOp::Eq:
  case BinaryOp::Ne:
    if (!equal)
      return {};
    if (integers || booleans) {
      if (op == BinaryOp::Eq)
        return ScalarComparison{Relation::Eq};
      return ScalarComparison{Relation::Ne};
    }
    if (op == BinaryOp::Eq)
      return StructuralEquality{EqualityOp::Eq};
    return StructuralEquality{EqualityOp::Ne};
  case BinaryOp::Lt:
  case BinaryOp::Le:
  case BinaryOp::Gt:
  case BinaryOp::Ge:
    if (!equal || (!integers && !booleans))
      return {};
    switch (op) {
    case BinaryOp::Lt:
      return ScalarComparison{Relation::Lt};
    case BinaryOp::Le:
      return ScalarComparison{Relation::Le};
    case BinaryOp::Gt:
      return ScalarComparison{Relation::Gt};
    default:
      return ScalarComparison{Relation::Ge};
    }
  case BinaryOp::LogicalAnd:
  case BinaryOp::LogicalOr:
    if (!booleans)
      return {};
    if (op == BinaryOp::LogicalAnd)
      return LogicalOperation{LogicalOp::And};
    return LogicalOperation{LogicalOp::Or};
  }
  return {};
}
ast::BinaryOp binary_operation(ast::CompoundAssignOp op) {
  using CompoundOp = ast::CompoundAssignOp;
  using BinaryOp = ast::BinaryOp;
  switch (op) {
  case CompoundOp::Add:
    return BinaryOp::Add;
  case CompoundOp::Sub:
    return BinaryOp::Sub;
  case CompoundOp::Mul:
    return BinaryOp::Mul;
  case CompoundOp::Div:
    return BinaryOp::Div;
  case CompoundOp::Rem:
    return BinaryOp::Rem;
  case CompoundOp::BitAnd:
    return BinaryOp::BitAnd;
  case CompoundOp::BitOr:
    return BinaryOp::BitOr;
  case CompoundOp::BitXor:
    return BinaryOp::BitXor;
  case CompoundOp::Shl:
    return BinaryOp::Shl;
  case CompoundOp::Shr:
    return BinaryOp::Shr;
  }
  throw std::invalid_argument("invalid compound operator");
}
} // namespace rx
