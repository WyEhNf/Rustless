#include "checks.hpp"

namespace rx {

void SafeGate::validate_tree() {
  std::unordered_set<ast::NodeId> identities;
  std::unordered_set<ast::NodeId> expression_nodes;
  std::unordered_set<ast::NodeId> type_nodes;

  ast::walk(*crate_, [&](const ast::Node &node, const auto &) {
    if (!node.id.value || !identities.insert(node.id).second ||
        node.span.file != crate_->source.id ||
        node.span.begin > node.span.end ||
        node.span.end > crate_->source.text.size())
      throw std::invalid_argument("invalid AST identity or source span");
    nodes_.emplace(node.id, &node);
  });
  auto check_ids = [&](const auto &facts) {
    for (const auto &[id, _] : facts)
      if (identities.find(id) == identities.end())
        throw std::invalid_argument("semantic fact refers outside its AST");
  };
  check_ids(declarations_);
  check_ids(expressions_);
  check_ids(types_);
  ast::walk(*crate_, [&](const auto &owner, const auto &) {
    using Payload = std::decay_t<decltype(owner)>;

    if constexpr (std::is_same_v<Payload, ast::TypePredicate>)
      return ast::WalkControl::SkipChildren;
    if constexpr (std::is_same_v<Payload, ast::Expr> ||
                  std::is_same_v<Payload, ast::Block> ||
                  std::is_same_v<Payload, ast::IfBranch>) {
      (void)expression(owner.id);
      expression_nodes.insert(owner.id);
    }
    if constexpr (std::is_same_v<Payload, ast::TypeSyntax>) {
      (void)type(owner.id);
      type_nodes.insert(owner.id);
    }
    return ast::WalkControl::Continue;
  });
  if (expression_nodes.size() != expressions_.size() ||
      type_nodes.size() != types_.size())
    throw std::invalid_argument("semantic fact is attached to wrong AST shape");
}

} // namespace rx
