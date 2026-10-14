#include "rx/semantic/analyzer.hpp"

namespace rx::semantic_detail {

ExprInfo Analyzer::check_block(const ast::Block &block,
                               const SemanticTypePtr &expected) {
  scopes_.emplace_back();
  SemanticTypePtr result = primitive_type(SemanticType::Kind::Unit);
  ControlFlow flow;
  for (const auto &statement : block.statements) {
    ast::visit(
        *statement,
        ast::Overloaded{
            [&](const ast::Stmt &node, const ast::EmptyStmt &) {},
            [&](const ast::Stmt &node, const ast::LetStmt &payload) {
              SemanticTypePtr annotation;
              if (payload.annotation)
                annotation = resolve_type(*payload.annotation, current_owner());
              auto initializer = check_expr(payload.initializer, annotation);
              flow = flow.then(flow_of(payload.initializer->id));
              auto binding_type = initializer.type;
              if (annotation)
                binding_type = annotation;
              if (annotation && !coercible(initializer.type, annotation))
                mismatch(payload.initializer->span, initializer.type,
                         annotation);
              if (annotation && annotation->kind() == SemanticType::Kind::Ref &&
                  annotation->is_mutable() && initializer.access_locked)
                report(payload.initializer->span,
                       "mutable reference initializer requires mutable "
                       "container access");
              std::optional<ast::NodeId> owner;
              if (current_function_)
                owner = current_function_->ast->id;
              record_symbol(node.id, SymbolKind::Local,
                            payload.binding.name.text, owner);
              record_symbol_type(node.id, binding_type);
              if (protected_builtin_value(payload.binding.name.text))
                report(payload.binding.name.span,
                       "cannot shadow a protected builtin");
              scopes_.back()[payload.binding.name.text] = {
                  binding_type,
                  payload.binding.mutability == ast::Mutability::Mutable,
                  symbol_for(node.id)};
            },
            [&](const ast::Stmt &node, const ast::ExprStmt &payload) {
              auto value = check_expr(payload.expression);
              flow = flow.then(flow_of(payload.expression->id));
              if (payload.termination == ast::Termination::Omitted &&
                  value.type->kind() != SemanticType::Kind::Unit &&
                  value.type->kind() != SemanticType::Kind::Never)
                report(payload.expression->span,
                       "a non-final expression without a "
                       "semicolon must have unit type");
            }});
  }
  // Check all statements and the tail even after normal execution has ended.
  // A tail always supplies the static type, while a tail-less diverging block is !.
  if (block.tail) {
    result = check_expr(*block.tail, expected).type;
    flow = flow.then(flow_of((*block.tail)->id));
  } else if (!flow.completes_normally())
    result = primitive_type(SemanticType::Kind::Never);
  scopes_.pop_back();
  control_flows_[block.id] = flow;
  record_node(block, ExprInfo{result});
  return {result};
}

ExprInfo Analyzer::check_if(const ast::Node &node, const ast::IfExpr &branch,
                            const SemanticTypePtr &expected) {
  auto condition =
      check_expr(branch.condition, primitive_type(SemanticType::Kind::Bool));
  if (!compatible_types(condition.type,
                        primitive_type(SemanticType::Kind::Bool)))
    mismatch(branch.condition->span, condition.type,
             primitive_type(SemanticType::Kind::Bool));
  auto then_value = check_block(*branch.then_block, expected);
  if (!branch.else_branch) {
    if (then_value.type->kind() != SemanticType::Kind::Unit &&
        then_value.type->kind() != SemanticType::Kind::Never)
      report(node.span, "if without else requires a unit consequent");
    return {primitive_type(SemanticType::Kind::Unit)};
  }
  ast::NodeId else_id;
  auto else_value = std::visit(
      ast::Overloaded{[&](const ast::Box<ast::Block> &payload) {
                        else_id = payload->id;
                        return check_block(*payload, expected);
                      },
                      [&](const ast::Box<ast::IfBranch> &payload) {
                        else_id = payload->id;
                        auto result = ast::visit(
                            *payload, [&](const ast::IfBranch &owner,
                                          const ast::IfExpr &payload) {
                              return check_if(owner, payload, expected);
                            });
                        record_node(*payload, result);
                        return result;
                      }},
      *branch.else_branch);
  if (then_value.type->kind() == SemanticType::Kind::Never)
    return else_value;
  if (else_value.type->kind() == SemanticType::Kind::Never)
    return then_value;
  if (coercible(else_value.type, then_value.type)) {
    append_adjustments(else_id,
                       coercion_adjustments(else_value.type, then_value.type));
    return then_value;
  }
  if (coercible(then_value.type, else_value.type)) {
    append_adjustments(branch.then_block->id,
                       coercion_adjustments(then_value.type, else_value.type));
    return else_value;
  }
  mismatch(node.span, else_value.type, then_value.type);
  return {};
}

ExprInfo Analyzer::check_loop(const ast::Expr &node,
                              const ast::LoopExpr &loop_expr,
                              const SemanticTypePtr &expected) {
  loops_.push_back({expected, {}});
  auto body = check_block(*loop_expr.body);
  if (body.type->kind() != SemanticType::Kind::Unit &&
      body.type->kind() != SemanticType::Kind::Never)
    report(loop_expr.body->span, "loop body must have unit type");
  auto loop = std::move(loops_.back());
  loops_.pop_back();
  if (loop.break_types.empty())
    return {primitive_type(SemanticType::Kind::Never)};
  auto result = loop.break_types.front();
  if (loop.expected)
    result = loop.expected;
  for (const auto &value : loop.break_types)
    if (!coercible(value, result))
      mismatch(node.span, value, result);
  return {result};
}

ExprInfo Analyzer::check_while(const ast::Expr &node,
                               const ast::WhileExpr &while_expr) {

  auto surrounding_loops = std::move(loops_);
  loops_.clear();
  auto condition = check_expr(while_expr.condition,
                              primitive_type(SemanticType::Kind::Bool));
  loops_ = std::move(surrounding_loops);
  if (!compatible_types(condition.type,
                        primitive_type(SemanticType::Kind::Bool)))
    mismatch(while_expr.condition->span, condition.type,
             primitive_type(SemanticType::Kind::Bool));
  loops_.push_back({primitive_type(SemanticType::Kind::Unit), {}});
  auto body =
      check_block(*while_expr.body, primitive_type(SemanticType::Kind::Unit));
  if (body.type->kind() != SemanticType::Kind::Unit &&
      body.type->kind() != SemanticType::Kind::Never)
    report(while_expr.body->span, "while body must have unit type");
  for (const auto &value : loops_.back().break_types)
    if (value->kind() != SemanticType::Kind::Unit)
      report(node.span, "break with a value is not allowed in while");
  loops_.pop_back();
  return {primitive_type(SemanticType::Kind::Unit)};
}

ExprInfo Analyzer::check_break(const ast::Expr &node,
                               const ast::BreakExpr &payload) {
  if (loops_.empty()) {
    report(node.span, "break is only available inside a loop");
    return {primitive_type(SemanticType::Kind::Never)};
  }
  ExprInfo value{primitive_type(SemanticType::Kind::Unit)};
  if (payload.value)
    value = check_expr(*payload.value, loops_.back().expected);
  loops_.back().break_types.push_back(value.type);
  return {primitive_type(SemanticType::Kind::Never)};
}

ExprInfo Analyzer::check_continue(const ast::Expr &node,
                                  const ast::ContinueExpr &) {
  if (loops_.empty())
    report(node.span, "continue is only available inside a loop");
  return {primitive_type(SemanticType::Kind::Never)};
}

ExprInfo Analyzer::check_return(const ast::Expr &node,
                                const ast::ReturnExpr &payload) {
  auto expected = primitive_type(SemanticType::Kind::Error);
  if (current_function_)
    expected = current_function_->result;
  ExprInfo value{primitive_type(SemanticType::Kind::Unit)};
  if (payload.value)
    value = check_expr(*payload.value, expected);
  if (!coercible(value.type, expected))
    mismatch(node.span, value.type, expected);
  return {primitive_type(SemanticType::Kind::Never)};
}

} // namespace rx::semantic_detail
