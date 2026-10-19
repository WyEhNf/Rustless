#include "semantic/analyzer.hpp"

#include <limits>

namespace rx::semantic_detail {

ExprInfo Analyzer::check_borrow(const ast::Expr &node,
                                const ast::BorrowExpr &borrow) {
  auto operand = check_expr(borrow.operand);
  bool mutable_ref = borrow.mutability == ast::Mutability::Mutable;
  if (mutable_ref &&
      ((operand.is_place && !operand.is_mutable) || operand.access_locked))
    report(borrow.operator_span, "mutable borrow requires a mutable place");
  return {compound(SemanticType::Kind::Ref, operand.type, mutable_ref)};
}

ExprInfo Analyzer::check_unary(const ast::Expr &node,
                               const ast::UnaryExpr &unary,
                               const SemanticTypePtr &expected) {
  SemanticTypePtr expected_operand = expected;
  if (unary.op == ast::UnaryOp::Dereference)
    expected_operand.reset();
  auto operand = check_expr(unary.operand, expected_operand);
  if (unary.op == ast::UnaryOp::Dereference) {
    if (operand.type->kind() != SemanticType::Kind::Ref &&
        operand.type->kind() != SemanticType::Kind::Box) {
      report(node.span,
             "type " + type_name(operand.type) + " cannot be dereferenced");
      return {};
    }
    const bool shared_layer = operand.type->kind() == SemanticType::Kind::Ref &&
                              !operand.type->is_mutable();
    const bool locked = operand.access_locked || shared_layer;
    bool mutable_access = operand.is_mutable;
    if (operand.type->kind() == SemanticType::Kind::Ref)
      mutable_access = operand.type->is_mutable();
    mutable_access = mutable_access && !locked;
    ExprInfo result{operand.type->element(), true, mutable_access, locked};
    result.unary =
        ResolvedUnary{UnaryOperation::Dereference,
                      operand_use(*unary.operand, operand, false), result.type};
    return result;
  }
  auto use = operand_use(*unary.operand, operand);
  if (unary.op == ast::UnaryOp::Not) {
    if (!integer(use.effective_type) &&
        use.effective_type->kind() != SemanticType::Kind::Bool)
      report(node.span, "operator ! requires bool or integer");
  } else if (!signed_integer(use.effective_type)) {
    report(node.span, "unary minus requires a signed integer");
  }
  ExprInfo result{use.effective_type};
  auto operation = UnaryOperation::Negate;
  if (unary.op == ast::UnaryOp::Not)
    operation = UnaryOperation::Complement;
  result.unary = ResolvedUnary{operation, std::move(use), result.type};
  return result;
}

OperandSemantics Analyzer::operand_use(const ast::Expr &node,
                                       const ExprInfo &info, bool references,
                                       unsigned depth_limit,
                                       bool mutable_references) {
  OperandSemantics use{node.id, info.type, {}};

  for (const auto &adjustment : info.adjustments) {
    use.adjustments.push_back(adjustment);
    use.effective_type = adjustment.target;
  }
  for (unsigned depth = 0;
       references && depth < depth_limit &&
       use.effective_type->kind() == SemanticType::Kind::Ref &&
       (mutable_references || !use.effective_type->is_mutable());
       ++depth) {
    use.effective_type = use.effective_type->element();
    use.adjustments.push_back(
        {Adjustment::Kind::Dereference, use.effective_type});
  }
  return use;
}

ExprInfo Analyzer::check_binary(const ast::Expr &node,
                                const ast::BinaryExpr &binary) {
  auto left = check_expr(binary.left);
  auto right = check_expr(binary.right, left.type);
  const bool logical = binary.op == ast::BinaryOp::LogicalAnd ||
                       binary.op == ast::BinaryOp::LogicalOr;
  const bool ordering =
      binary.op == ast::BinaryOp::Lt || binary.op == ast::BinaryOp::Le ||
      binary.op == ast::BinaryOp::Gt || binary.op == ast::BinaryOp::Ge;
  unsigned depth = 1;
  if (ordering)
    depth = std::numeric_limits<unsigned>::max();
  auto lhs = operand_use(*binary.left, left, !logical, depth, ordering);
  auto rhs = operand_use(*binary.right, right, !logical, depth, ordering);
  if (ordering) {
    auto left_type = left.type;
    auto right_type = right.type;
    if ((left_type->kind() == SemanticType::Kind::Ref) !=
        (right_type->kind() == SemanticType::Kind::Ref))
      report(node.span, "ordering requires matching scalar operands");
    while (left_type->kind() == SemanticType::Kind::Ref &&
           right_type->kind() == SemanticType::Kind::Ref) {
      if (left_type->is_mutable() && !right_type->is_mutable())
        report(node.span, "ordering cannot coerce a mutable left reference "
                          "to a shared right reference");
      left_type = left_type->element();
      right_type = right_type->element();
    }
  }
  auto operation =
      resolve_operation(binary.op, lhs.effective_type, rhs.effective_type);
  if (!operation) {
    report(node.span, "operator " + std::string(ast::spelling(binary.op)) +
                          " is not defined for " +
                          type_name(lhs.effective_type) + " and " +
                          type_name(rhs.effective_type));
    return {};
  }
  if ((binary.op == ast::BinaryOp::Eq || binary.op == ast::BinaryOp::Ne) &&
      !supports(lhs.effective_type, "PartialEq", "", {}))
    report(node.span, "equality requires PartialEq");
  const bool boolean = std::holds_alternative<ScalarComparison>(*operation) ||
                       std::holds_alternative<StructuralEquality>(*operation) ||
                       logical;
  auto result_type = lhs.effective_type;
  if (boolean)
    result_type = primitive_type(SemanticType::Kind::Bool);
  ExprInfo result{result_type};
  result.binary =
      ResolvedBinary{*operation, std::move(lhs), std::move(rhs), result.type};
  return result;
}

ExprInfo Analyzer::check_assignment(const ast::Expr &node,
                                    const ast::AssignExpr &assignment) {
  auto destination = check_expr(assignment.target);
  auto source = check_expr(assignment.value, destination.type);
  check_assignment_target(assignment.target, destination);
  if (!coercible(source.type, destination.type))
    mismatch(assignment.value->span, source.type, destination.type);
  return {primitive_type(SemanticType::Kind::Unit)};
}

void Analyzer::check_assignment_target(const ast::ExprPtr &target,
                                       const ExprInfo &destination) {
  if (!destination.is_place)
    report(target->span, "assignment destination is not a place");
  else if (!destination.is_mutable)
    report(target->span, "assignment destination is immutable");
}

ExprInfo
Analyzer::check_compound_assignment(const ast::Expr &node,
                                    const ast::CompoundAssignExpr &assignment) {
  auto destination = check_expr(assignment.target);
  auto source = check_expr(assignment.value, destination.type);
  check_assignment_target(assignment.target, destination);
  auto lhs = operand_use(*assignment.target, destination, false);
  auto rhs = operand_use(*assignment.value, source);
  auto operation = resolve_operation(binary_operation(assignment.op),
                                     lhs.effective_type, rhs.effective_type);
  if (!operation) {
    report(node.span,
           "compound operator " + std::string(ast::spelling(assignment.op)) +
               " is not defined for " + type_name(lhs.effective_type) +
               " and " + type_name(rhs.effective_type));
    return {primitive_type(SemanticType::Kind::Unit)};
  }
  ExprInfo result{primitive_type(SemanticType::Kind::Unit)};
  result.compound = ResolvedBinary{*operation, std::move(lhs), std::move(rhs),
                                   destination.type};
  return result;
}

ExprInfo Analyzer::check_cast(const ast::Expr &node,
                              const ast::CastExpr &cast) {
  auto source = check_expr(cast.value);
  auto destination = resolve_type(cast.target_type, current_owner());
  const bool valid =
      (integer(source.type) && integer(destination)) ||
      (source.type->kind() == SemanticType::Kind::Bool && integer(destination));
  if (!valid)
    report(node.span, "unsupported cast from " + type_name(source.type) +
                          " to " + type_name(destination));
  return {destination};
}

} // namespace rx::semantic_detail
