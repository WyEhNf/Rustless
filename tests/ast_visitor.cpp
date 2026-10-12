#include "rx/ast/traversal.hpp"
#include "rx/frontend.hpp"

#undef NDEBUG
#include <cassert>
#include <type_traits>

namespace ast = rx::ast;
struct IdentityVisitor {
  template <class Payload>
  ast::NodeId operator()(const ast::Node &node, const Payload &) const {
    return node.id;
  }
};
struct OnlyBinary {
  void operator()(const ast::Expr &, const ast::BinaryExpr &) {}
};
struct DifferentReturns {
  template <class Payload> int operator()(const ast::Node &, const Payload &) {
    return 0;
  }
  void operator()(const ast::Expr &, const ast::BinaryExpr &) {}
};
struct MutablePayload {
  template <class Payload, std::enable_if_t<!std::is_const_v<Payload>, int> = 0>
  void operator()(const ast::Node &, Payload &) {}
};
struct ReferenceVisitor {
  template <class Payload>
  const ast::NodeId &operator()(const ast::Node &node, const Payload &) {
    return node.id;
  }
};
struct WrongOwner {
  template <class Payload>
  void operator()(const ast::TypeSyntax &, const Payload &) {}
};
struct DifferentItems {
  template <class Payload>
  int operator()(const ast::Node &, const Payload &) const {
    return 0;
  }
  void operator()(const ast::Function &, const ast::Function &) const {}
};
static_assert(ast::ExhaustiveVisitor<IdentityVisitor, ast::Expr>);
static_assert(ast::ExhaustiveVisitor<IdentityVisitor, ast::TypeSyntax>);
static_assert(ast::ExhaustiveVisitor<IdentityVisitor, ast::Stmt>);
static_assert(ast::ExhaustiveVisitor<IdentityVisitor, ast::Item>);
static_assert(ast::ExhaustiveVisitor<IdentityVisitor, ast::ImplMember>);
static_assert(ast::ExhaustiveVisitor<IdentityVisitor, ast::WherePredicate>);
static_assert(ast::ExhaustiveVisitor<IdentityVisitor, ast::UseTree>);
static_assert(ast::ExhaustiveVisitor<IdentityVisitor, ast::SelfParam>);
static_assert(ast::ExhaustiveVisitor<IdentityVisitor, ast::IfBranch>);
static_assert(!ast::ExhaustiveVisitor<OnlyBinary, ast::Expr>);
static_assert(!ast::ExhaustiveVisitor<DifferentReturns, ast::Expr>);
static_assert(!ast::ExhaustiveVisitor<MutablePayload, ast::Expr>);
static_assert(!ast::ExhaustiveVisitor<IdentityVisitor, int>);
static_assert(!ast::ExhaustiveVisitor<WrongOwner, ast::Expr>);
static_assert(!ast::ExhaustiveVisitor<DifferentItems, ast::Item>);
static_assert(
    std::is_same_v<decltype(ast::visit(std::declval<const ast::Expr &>(),
                                       std::declval<ReferenceVisitor &>())),
                   const ast::NodeId &>);

struct BinaryObserver : ast::DefaultVisitor {
  using DefaultVisitor::enter;
  int count{};
  ast::WalkControl enter(const ast::Expr &owner,
                         const ast::BinaryExpr &Payload) {
    assert(owner.id.value && Payload.left->id != Payload.right->id);
    ++count;
    return ast::WalkControl::Continue;
  }
};
struct TraceVisitor {
  ast::NodeId target{};
  ast::WalkControl action{ast::WalkControl::Continue};
  std::vector<ast::NodeId> entered, left, stack;
  template <class Payload>
  ast::WalkControl enter(const ast::Node &node, const Payload &) {
    entered.push_back(node.id);
    stack.push_back(node.id);
    if (node.id == target) {
      return action;
    }
    return ast::WalkControl::Continue;
  }
  template <class Payload> void leave(const ast::Node &node, const Payload &) {
    assert(!stack.empty() && stack.back() == node.id);
    stack.pop_back();
    left.push_back(node.id);
  }
};
struct MissingLeave {
  template <class Payload>
  ast::WalkControl enter(const ast::Node &, const Payload &) {
    return ast::WalkControl::Continue;
  }
};
struct WrongControl : ast::DefaultVisitor {
  using DefaultVisitor::enter;
  bool enter(const ast::Expr &, const ast::BinaryExpr &) { return true; }
};
struct MissingBinary {
  template <
      class Payload,
      std::enable_if_t<!std::is_same_v<Payload, ast::BinaryExpr>, int> = 0>
  ast::WalkControl enter(const ast::Node &, const Payload &) {
    return ast::WalkControl::Continue;
  }
  template <class Payload> void leave(const ast::Node &, const Payload &) {}
};
struct BothInterfaces : BinaryObserver {
  int callback_calls{};
  template <class Payload> void operator()(const ast::Node &, const Payload &) {
    ++callback_calls;
  }
};
template <class Owner, class Enable = void>
struct WalkIsAvailable : std::false_type {};

template <class Owner>
struct WalkIsAvailable<
    Owner, std::void_t<decltype(ast::walk(std::declval<const Owner &>(),
                                          std::declval<TraceVisitor &>()))>>
    : std::true_type {};

template <class Owner>
inline constexpr bool CanWalk = WalkIsAvailable<Owner>::value;
static_assert(!ast::TraversalVisitor<MissingBinary>);
static_assert(CanWalk<ast::Expr> && CanWalk<ast::Item>);
static_assert(!CanWalk<int> && !CanWalk<ast::Node>);
static_assert(!ast::SyntaxRoot<ast::BinaryExpr>);
static_assert(ast::TraversalVisitor<BinaryObserver>);
static_assert(ast::TraversalVisitor<TraceVisitor>);
static_assert(!ast::TraversalVisitor<MissingLeave>);
static_assert(!ast::TraversalVisitor<WrongControl>);
static_assert(!ast::TraversalCallback<OnlyBinary>);
static_assert(!ast::TraversalCallback<IdentityVisitor>);

void check_walk(const ast::Crate &crate, const ast::Expr &expr) {
  const auto &binary = *ast::get_if<ast::BinaryExpr>(expr);
  BinaryObserver observer;
  assert(ast::walk(crate, observer) == ast::WalkResult::Completed);
  assert(observer.count == 1);
  BothInterfaces both;
  assert(ast::walk(crate, both) == ast::WalkResult::Completed);
  assert(both.count == 1 && both.callback_calls == 0);
  TraceVisitor subtree;
  assert(ast::walk(expr, subtree) == ast::WalkResult::Completed);
  assert(subtree.entered.size() == 3 && subtree.left.size() == 3);
  TraceVisitor item;
  assert(ast::walk(crate.items[0], item) == ast::WalkResult::Completed);
  assert(item.entered.size() == 6 && item.left.size() == 6);
  TraceVisitor full;
  assert(ast::walk(crate, full) == ast::WalkResult::Completed);
  assert(full.entered.size() == 7 && full.left.size() == 7 &&
         full.stack.empty());
  assert(full.entered[4] == expr.id && full.entered[5] == binary.left->id &&
         full.entered[6] == binary.right->id);
  TraceVisitor skipped;
  skipped.target = expr.id;
  skipped.action = ast::WalkControl::SkipChildren;
  assert(ast::walk(crate, skipped) == ast::WalkResult::Completed);
  assert(skipped.entered.size() == 5 && skipped.left.size() == 5 &&
         skipped.stack.empty());
  TraceVisitor stopped;
  stopped.target = binary.left->id;
  stopped.action = ast::WalkControl::Stop;
  assert(ast::walk(crate, stopped) == ast::WalkResult::Stopped);
  assert(stopped.entered.size() == 6 && stopped.left.size() == 6 &&
         stopped.stack.empty());
  TraceVisitor root_stop;
  root_stop.target = crate.id;
  root_stop.action = ast::WalkControl::Stop;
  assert(ast::walk(crate, root_stop) == ast::WalkResult::Stopped);
  assert(root_stop.entered.size() == 1 && root_stop.left.size() == 1);
  std::vector<ast::NodeId> callback_ids;
  assert(ast::walk(crate, [&](const ast::Node &node, const auto &) {
           callback_ids.push_back(node.id);
         }) == ast::WalkResult::Completed);
  assert(callback_ids == full.entered);
  callback_ids.clear();
  assert(ast::walk(crate, [&](const ast::Node &node, const auto &) {
           callback_ids.push_back(node.id);
           if (node.id == expr.id) {
             return ast::WalkControl::Stop;
           }
           return ast::WalkControl::Continue;
         }) == ast::WalkResult::Stopped);
  assert(callback_ids.size() == 5);
}

int main() {
  rx::DiagnosticEngine diagnostics;
  auto parsed = rx::parse_source("fn main() { 1 + 2; }", diagnostics);
  assert(parsed.syntax_ok && !diagnostics.has_errors());
  const auto &function = std::get<ast::Function>(parsed.crate->items[0]);
  const auto &statement = *function.body->statements[0];
  const auto &expr = *std::get<ast::ExprStmt>(statement.data).expression;
  const auto id = ast::visit(
      expr, ast::Overloaded{
                [&](const ast::Expr &owner, const ast::BinaryExpr &value) {
                  assert(&owner == &expr &&
                         &value == ast::get_if<ast::BinaryExpr>(expr));
                  assert(parsed.crate->source.slice(owner.span) == "1 + 2");
                  assert(value.op == ast::BinaryOp::Add);
                  return owner.id;
                },
                IdentityVisitor{}});
  assert(id == expr.id);
  assert(&ast::visit(expr, ReferenceVisitor{}) == &expr.id);
  assert(ast::visit(parsed.crate->items[0], IdentityVisitor{}) == function.id);
  std::vector<ast::NodeId> child_ids;
  ast::for_each_child(
      expr, [&](const ast::Node &child) { child_ids.push_back(child.id); });
  const auto &binary = *ast::get_if<ast::BinaryExpr>(expr);
  assert((child_ids == std::vector{binary.left->id, binary.right->id}));
  child_ids.clear();
  ast::for_each_child(parsed.crate->items[0], [&](const ast::Node &child) {
    child_ids.push_back(child.id);
  });
  assert((child_ids == std::vector{function.body->id}));
  check_walk(*parsed.crate, expr);
}
