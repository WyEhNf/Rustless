#include "rx/ast/children.hpp"
#include "rx/semantic/analyzer.hpp"

#include <type_traits>

namespace rx::semantic_detail {

ControlFlow Analyzer::flow_of(ast::NodeId node) const {
  auto found = control_flows_.find(node);
  // A prior semantic error may have skipped checking this child.
  return found == control_flows_.end() ? ControlFlow{} : found->second;
}

ControlFlow Analyzer::branch_flow(const ast::IfExpr &branch) const {
  auto otherwise = ControlFlow{};
  if (branch.else_branch)
    otherwise = std::visit(
        [&](const auto &alternative) {
          using Branch = std::decay_t<decltype(*alternative)>;
          if constexpr (std::is_same_v<Branch, ast::Block>)
            return flow_of(alternative->id);
          else
            return branch_flow(alternative->data);
        },
        *branch.else_branch);
  auto alternatives = flow_of(branch.then_block->id).exits | otherwise.exits;
  return flow_of(branch.condition->id).then({alternatives});
}

ControlFlow Analyzer::expression_flow(const ast::Expr &expression) const {
  return ast::visit(
      expression,
      ast::Overloaded{
          [&](const ast::Expr &, const ast::BlockExpr &block) {
            return flow_of(block.block->id);
          },
          [&](const ast::Expr &, const ast::IfExpr &branch) {
            return branch_flow(branch);
          },
          [&](const ast::Expr &, const ast::LoopExpr &loop) -> ControlFlow {
            auto body = flow_of(loop.body->id);
            // Only this loop's breaks escape normally; nested loops consume
            // their own break/continue exits. Unreachable breaks still type-check.
            return {(body.exits & ControlFlow::Returned) |
                    ((body.exits & ControlFlow::Broken) ? ControlFlow::Normal
                                                        : 0u)};
          },
          [&](const ast::Expr &, const ast::WhileExpr &loop) {
            auto body = flow_of(loop.body->id);
            return flow_of(loop.condition->id)
                .then({ControlFlow::Normal |
                       (body.exits & ControlFlow::Returned)});
          },
          [&](const ast::Expr &, const ast::ReturnExpr &transfer) {
            auto value =
                transfer.value ? flow_of((*transfer.value)->id) : ControlFlow{};
            return value.then({ControlFlow::Returned});
          },
          [&](const ast::Expr &, const ast::BreakExpr &transfer) {
            auto value =
                transfer.value ? flow_of((*transfer.value)->id) : ControlFlow{};
            return value.then({ControlFlow::Broken});
          },
          [&](const ast::Expr &, const ast::ContinueExpr &) -> ControlFlow {
            return {ControlFlow::Continued};
          },
          [&](const ast::Expr &, const ast::BinaryExpr &binary) {
            auto right = flow_of(binary.right->id);
            if (binary.op == ast::BinaryOp::LogicalAnd ||
                binary.op == ast::BinaryOp::LogicalOr)
              right.exits |= ControlFlow::Normal;
            return flow_of(binary.left->id).then(right);
          },
          [&](const ast::Expr &, const ast::StructExpr &structure) {
            ControlFlow flow;
            for (const auto &field : structure.fields)
              flow = flow.then(flow_of(field.value->id));
            return flow;
          },
          [&](const ast::Expr &node, const auto &) {
            ControlFlow flow;
            // Other forms eagerly evaluate their expression children. Type
            // arguments and annotations are compile-time syntax, not operands.
            ast::for_each_child(node, [&](const auto &child) {
              if constexpr (std::is_same_v<std::decay_t<decltype(child)>,
                                           ast::Expr>)
                flow = flow.then(flow_of(child.id));
            });
            return flow;
          }});
}

} // namespace rx::semantic_detail
