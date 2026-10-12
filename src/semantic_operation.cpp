#include "rx/semantic_operation.hpp"
#include <stdexcept>

namespace rx {
namespace {
bool integer(const SemanticTypePtr &type) {
  using K = SemanticType::Kind;
  return type && (type->kind() == K::I32 || type->kind() == K::U32 ||
                  type->kind() == K::Isize || type->kind() == K::Usize);
}
} // namespace
std::optional<ResolvedOperation> resolve_operation(
    ast::BinaryOp op, const SemanticTypePtr &left,
    const SemanticTypePtr &right) {
  using A = ast::BinaryOp;
  using K = SemanticType::Kind;
  if (!left || !right) return {};
  const bool equal = same_type(left, right);
  const bool integers = integer(left) && integer(right);
  const bool booleans = left->kind() == K::Bool && right->kind() == K::Bool;
  switch (op) {
  case A::Add: case A::Sub: case A::Mul: case A::Div: case A::Rem:
    if (!integers || !equal) return {};
    switch (op) {
    case A::Add: return IntegerArithmetic{ArithmeticOp::Add};
    case A::Sub: return IntegerArithmetic{ArithmeticOp::Sub};
    case A::Mul: return IntegerArithmetic{ArithmeticOp::Mul};
    case A::Div: return IntegerArithmetic{ArithmeticOp::Div};
    default: return IntegerArithmetic{ArithmeticOp::Rem};
    }
  case A::BitAnd: case A::BitOr: case A::BitXor:
    if (!equal || (!integers && !booleans)) return {};
    return BitwiseOperation{op == A::BitAnd ? BitwiseOp::And
                           : op == A::BitOr ? BitwiseOp::Or : BitwiseOp::Xor};
  case A::Shl: case A::Shr:
    if (!integers) return {};
    return ShiftOperation{op == A::Shl ? ShiftDirection::Left
                                     : ShiftDirection::Right};
  case A::Eq: case A::Ne:
    if (!equal) return {};
    if (integers || booleans)
      return ScalarComparison{op == A::Eq ? Relation::Eq : Relation::Ne};
    return StructuralEquality{op == A::Eq ? EqualityOp::Eq : EqualityOp::Ne};
  case A::Lt: case A::Le: case A::Gt: case A::Ge:
    if (!equal || (!integers && !booleans)) return {};
    switch (op) {
    case A::Lt: return ScalarComparison{Relation::Lt};
    case A::Le: return ScalarComparison{Relation::Le};
    case A::Gt: return ScalarComparison{Relation::Gt};
    default: return ScalarComparison{Relation::Ge};
    }
  case A::LogicalAnd: case A::LogicalOr:
    if (!booleans) return {};
    return LogicalOperation{op == A::LogicalAnd ? LogicalOp::And : LogicalOp::Or};
  }
  return {};
}
ast::BinaryOp binary_operation(ast::CompoundAssignOp op) {
  using C = ast::CompoundAssignOp;
  using B = ast::BinaryOp;
  switch (op) {
  case C::Add: return B::Add;
  case C::Sub: return B::Sub;
  case C::Mul: return B::Mul;
  case C::Div: return B::Div;
  case C::Rem: return B::Rem;
  case C::BitAnd: return B::BitAnd;
  case C::BitOr: return B::BitOr;
  case C::BitXor: return B::BitXor;
  case C::Shl: return B::Shl;
  case C::Shr: return B::Shr;
  }
  throw std::invalid_argument("invalid compound operator");
}
} // namespace rx
