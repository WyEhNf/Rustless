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
} // namespace rx::ir