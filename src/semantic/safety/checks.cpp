#include "checks.hpp"

namespace rx::safety_detail {

void require_equivalent(const SemanticTypePtr &left,
                        const SemanticTypePtr &right) {
  if (!same_type(left, right))
    throw std::invalid_argument("semantic declaration type mismatch");
}
ast::BinaryOp source_operator(const ResolvedOperation &operation) {
  using BinaryOp = ast::BinaryOp;
  return std::visit(
      ast::Overloaded{
          [](const IntegerArithmetic &payload) {
            constexpr std::array values{BinaryOp::Add, BinaryOp::Sub,
                                        BinaryOp::Mul, BinaryOp::Div,
                                        BinaryOp::Rem};
            return values.at(static_cast<std::size_t>(payload.op));
          },
          [](const BitwiseOperation &payload) {
            constexpr std::array values{BinaryOp::BitAnd, BinaryOp::BitOr,
                                        BinaryOp::BitXor};
            return values.at(static_cast<std::size_t>(payload.op));
          },
          [](const ShiftOperation &payload) {
            if (payload.count_policy != ShiftCountPolicy::MaskToWidth)
              throw std::invalid_argument("invalid shift count policy");
            constexpr std::array values{BinaryOp::Shl, BinaryOp::Shr};
            return values.at(static_cast<std::size_t>(payload.direction));
          },
          [](const ScalarComparison &payload) {
            constexpr std::array values{BinaryOp::Eq, BinaryOp::Ne,
                                        BinaryOp::Lt, BinaryOp::Le,
                                        BinaryOp::Gt, BinaryOp::Ge};
            return values.at(static_cast<std::size_t>(payload.relation));
          },
          [](const StructuralEquality &payload) {
            constexpr std::array values{BinaryOp::Eq, BinaryOp::Ne};
            return values.at(static_cast<std::size_t>(payload.op));
          },
          [](const LogicalOperation &payload) {
            constexpr std::array values{BinaryOp::LogicalAnd,
                                        BinaryOp::LogicalOr};
            return values.at(static_cast<std::size_t>(payload.op));
          }},
      operation);
}
void validate_operand(const SafeGate &index, const OperandSemantics &operand) {
  using TypeKind = SemanticType::Kind;
  auto current = index.expression(operand.node).type;
  for (const auto &adjustment : operand.adjustments) {
    const auto &target = adjustment.target;
    switch (adjustment.kind) {
    case Adjustment::Kind::Dereference:
      if (current->kind() != TypeKind::Ref && current->kind() != TypeKind::Box)
        throw std::invalid_argument("dereference adjustment has no referent");
      require_equivalent(current->element(), target);
      break;
    case Adjustment::Kind::BorrowShared:
    case Adjustment::Kind::BorrowMutable:
      if (target->kind() != TypeKind::Ref ||
          target->is_mutable() !=
              (adjustment.kind == Adjustment::Kind::BorrowMutable))
        throw std::invalid_argument(
            "borrow adjustment has wrong reference type");
      require_equivalent(current, target->element());
      break;
    case Adjustment::Kind::MutToShared:
      if (current->kind() != TypeKind::Ref || !current->is_mutable() ||
          target->kind() != TypeKind::Ref || target->is_mutable())
        throw std::invalid_argument("mutable-to-shared adjustment mismatch");
      require_equivalent(current->element(), target->element());
      break;
    case Adjustment::Kind::NeverToAny:
      if (current->kind() != TypeKind::Never)
        throw std::invalid_argument("never coercion has non-never source");
      break;
    default:
      throw std::invalid_argument("unknown operand adjustment");
    }
    current = target;
  }
  require_equivalent(current, operand.effective_type);
}
void validate_operation(const SafeGate &index, const ResolvedBinary &fact,
                        const ast::Expr &left, const ast::Expr &right,
                        ast::BinaryOp op) {
  if (fact.left.node != left.id || fact.right.node != right.id ||
      source_operator(fact.operation) != op)
    throw std::invalid_argument(
        "resolved operation does not match source operands");
  validate_operand(index, fact.left);
  validate_operand(index, fact.right);
  const auto expected = resolve_operation(op, fact.left.effective_type,
                                          fact.right.effective_type);
  if (!expected || expected->index() != fact.operation.index())
    throw std::invalid_argument(
        "resolved operation category is invalid for types");
  const bool boolean =
      std::holds_alternative<ScalarComparison>(fact.operation) ||
      std::holds_alternative<StructuralEquality>(fact.operation) ||
      std::holds_alternative<LogicalOperation>(fact.operation);
  if (boolean) {
    if (fact.result->kind() != SemanticType::Kind::Bool)
      throw std::invalid_argument("comparison or logical result is not bool");
  } else {
    require_equivalent(fact.result, fact.left.effective_type);
  }
}
bool same_target(const CallTarget &left, const CallTarget &right) {
  return std::visit(
      ast::Overloaded{[&](const FunctionTarget &target) {
                        const auto *other = std::get_if<FunctionTarget>(&right);
                        return other && target.symbol == other->symbol;
                      },
                      [&](const BuiltinTarget &target) {
                        const auto *other = std::get_if<BuiltinTarget>(&right);
                        return other && target.builtin == other->builtin;
                      }},
      left);
}

} // namespace rx::safety_detail
