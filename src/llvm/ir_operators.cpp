#include "rx/ir_semantic.hpp"
#include <stdexcept>

namespace rx::ir {
namespace {
using K = SemanticType::Kind;
bool signed_type(const SemanticTypePtr &type) {
  return type->kind == K::I32 || type->kind == K::Isize;
}
SemanticTypePtr scalar_operand(SemanticTypePtr type) {
  while (type && type->kind == K::Ref) type = type->element;
  if (!type) throw std::invalid_argument("missing scalar operand type");
  return type;
}
} // namespace
BinaryLowering SemanticBridge::binary(const ast::Expr &node,
                                      const ast::BinaryExpr &payload) const {
  if (std::get_if<ast::BinaryExpr>(&node.data) != &payload)
    throw std::invalid_argument("binary payload does not belong to this AST node");
  auto left = scalar_operand(semantic_.expression(*payload.left).type);
  auto operand = scalar_type(left);
  auto result = scalar_type(semantic_.expression(node).type);
  if (!operand || !result || *operand == Type::Void || *result == Type::Void)
    throw std::invalid_argument("aggregate operators need aggregate lowering");
  const bool sign = signed_type(left);
  using A = ast::BinaryOp;
  auto operation = [&]() -> decltype(BinaryLowering::operation) {
    switch (payload.op) {
    case A::Add: return BinaryOp::Add;
    case A::Sub: return BinaryOp::Sub;
    case A::Mul: return BinaryOp::Mul;
    case A::Div: return sign ? BinaryOp::SDiv : BinaryOp::UDiv;
    case A::Rem: return sign ? BinaryOp::SRem : BinaryOp::URem;
    case A::BitAnd: return BinaryOp::And;
    case A::BitOr: return BinaryOp::Or;
    case A::BitXor: return BinaryOp::Xor;
    case A::Shl: return BinaryOp::Shl;
    case A::Shr: return sign ? BinaryOp::AShr : BinaryOp::LShr;
    case A::Eq: return Predicate::Eq;
    case A::Ne: return Predicate::Ne;
    case A::Lt: return sign ? Predicate::SLt : Predicate::ULt;
    case A::Le: return sign ? Predicate::SLe : Predicate::ULe;
    case A::Gt: return sign ? Predicate::SGt : Predicate::UGt;
    case A::Ge: return sign ? Predicate::SGe : Predicate::UGe;
    case A::LogicalAnd: return ShortCircuit::And;
    case A::LogicalOr: return ShortCircuit::Or;
    }
    throw std::invalid_argument("invalid AST binary operator");
  }();
  return {operation, *operand, *result};
}
} // namespace rx::ir
