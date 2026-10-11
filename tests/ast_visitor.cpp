#include "rx/ast/dispatch.hpp"
#include "rx/frontend.hpp"

#undef NDEBUG
#include <cassert>
#include <type_traits>

namespace ast = rx::ast;
struct IdentityVisitor {
  template <class T> ast::NodeId operator()(const ast::Node &node, const T &) {
    return node.id;
  }
};
struct OnlyBinary {
  void operator()(const ast::Expr &, const ast::BinaryExpr &) {}
};
struct DifferentReturns {
  template <class T> int operator()(const ast::Node &, const T &) { return 0; }
  void operator()(const ast::Expr &, const ast::BinaryExpr &) {}
};
struct MutablePayload {
  template <class T> void operator()(const ast::Node &, T &) requires (!std::is_const_v<T>) {}
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

int main() {
  rx::DiagnosticEngine diagnostics;
  auto parsed = rx::parse_source("fn main() { 1 + 2; }", diagnostics);
  assert(parsed.syntax_ok && !diagnostics.has_errors());
  const auto &function = std::get<ast::Function>(parsed.crate->items[0]);
  const auto &statement = *function.body->statements[0];
  const auto &expr = *std::get<ast::ExprStmt>(statement.data).expression;
  const auto id = ast::dispatch(expr, ast::Overloaded{
    [&](const ast::Expr &owner, const ast::BinaryExpr &value) {
      assert(&owner == &expr && &value == ast::get_if<ast::BinaryExpr>(expr));
      assert(parsed.crate->source.slice(owner.span) == "1 + 2");
      assert(value.op == ast::BinaryOp::Add);
      return owner.id;
    }, IdentityVisitor{}});
  assert(id == expr.id);
  assert(ast::dispatch(parsed.crate->items[0], IdentityVisitor{}) == function.id);
}
