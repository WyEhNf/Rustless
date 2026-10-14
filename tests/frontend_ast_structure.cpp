#include "rx/ast/visitor.hpp"
#include "rx/ast/frontend.hpp"

#undef NDEBUG
#include <cassert>
#include <set>
#include <type_traits>

namespace ast = rx::ast;
static_assert(!std::is_default_constructible_v<ast::Expr>);
static_assert(!std::is_default_constructible_v<ast::ExprPtr>);
static_assert(!std::is_copy_constructible_v<ast::ExprPtr>);
static_assert(!std::is_default_constructible_v<ast::BinaryExpr>);
static_assert(!std::is_same_v<ast::BinaryOp, ast::CompoundAssignOp>);
static_assert(
    std::is_same_v<decltype(std::declval<const ast::ExprPtr &>().get()), const ast::Expr *>);

int main() {
  bool rejected_null = false;
  try {
    ast::ExprPtr invalid{std::unique_ptr<ast::Expr>{}};
  } catch (const std::invalid_argument &) {
    rejected_null = true;
  }
  assert(rejected_null);
  constexpr std::string_view source = R"(
use ::crate::io::{read as r, write as _, nested::{leaf, *}, *};
struct S<'a: 'b, 'b> where 'a: 'b, &'a i32: 'b {
    value: &'a mut Foo<'a, i32>::Bar<u32>
}
impl<'a> S<'a> where 'a: 'a {
    const A: usize = (2);
    fn f(&'a mut self, x: &&'a mut i32) -> (i32) where 'a: 'a {
        if true { 1 } else if false { 2 } else { 3 }
    }
    const B: usize = 3;
}
fn ops() {
    let mut x = 1;
    x += 2;
    x = (x + 2) * 3;
    let repeated = [x; (2usize)];
    x.foo::<i32>(1).bar[0];
    &&mut x;
    return ();
}
)";
  rx::DiagnosticEngine diagnostics;
  auto parsed = rx::parse_source(source, diagnostics);
  assert(parsed.syntax_ok && parsed.crate && !diagnostics.has_errors());
  const auto &crate = *parsed.crate;
  assert(crate.items.size() == 4);

  const auto &use = std::get<ast::Use>(crate.items[0]);
  assert(use.tree.prefix.root == ast::PathRoot::Absolute);
  const auto &group = std::get<ast::GroupImport>(use.tree.target);
  assert(group.children.size() == 4);
  const auto &rename = std::get<ast::NameImport>(group.children[0].target);
  assert(std::get<ast::Identifier>(*rename.alias).text == "r");
  const auto &underscore_import = std::get<ast::NameImport>(group.children[1].target);
  assert(std::holds_alternative<ast::UnderscoreAlias>(*underscore_import.alias));
  assert(std::get<ast::GroupImport>(group.children[2].target).children.size() == 2);
  assert(std::holds_alternative<ast::GlobImport>(group.children[3].target));

  const auto &structure = std::get<ast::Struct>(crate.items[1]);
  assert(structure.generics->lifetimes[0].bounds[0].text == "'b");
  assert(structure.where_clause->predicates.size() == 2);
  assert(std::holds_alternative<ast::TypePredicate>(structure.where_clause->predicates[1]));
  const auto &reference = std::get<ast::ReferenceType>(structure.fields[0].type->data);
  assert(reference.lifetime->text == "'a");
  const auto &path = std::get<ast::PathType>(reference.referent->data).path;
  assert(path.segments.size() == 2);
  assert(path.segments[0].arguments->values.size() == 2);
  assert(std::get<ast::LifetimeName>(path.segments[0].arguments->values[0]).text == "'a");
  assert(path.segments[1].arguments->values.size() == 1);

  const auto &implementation = std::get<ast::Impl>(crate.items[2]);
  assert(implementation.members.size() == 3);
  assert(std::get<ast::Constant>(implementation.members[2]).name.text == "B");
  const auto &function = std::get<ast::Function>(implementation.members[1]);
  assert(std::get<ast::ReferenceSelf>(function.signature.receiver->data).lifetime->text == "'a");
  assert(ast::get_if<ast::ParenType>(**function.signature.return_annotation));
  const auto &outer = std::get<ast::ReferenceType>(function.signature.parameters[0].type->data);
  assert(!outer.lifetime && outer.mutability == ast::Mutability::Immutable);
  assert(std::get<ast::ReferenceType>(outer.referent->data).lifetime->text == "'a");
  assert(outer.referent->span.begin == function.signature.parameters[0].type->span.begin + 1);
  const auto &branch = std::get<ast::IfExpr>((*function.body->tail)->data);
  assert(std::holds_alternative<ast::Box<ast::IfBranch>>(*branch.else_branch));

  const auto &ops = std::get<ast::Function>(crate.items[3]);
  assert(!ops.signature.return_annotation && !ops.body->tail);
  const auto &statements = ops.body->statements;
  auto expression = [&](std::size_t index) -> const ast::Expr & {
    return *std::get<ast::ExprStmt>(statements[index]->data).expression;
  };
  assert(std::get<ast::CompoundAssignExpr>(expression(1).data).op == ast::CompoundAssignOp::Add);
  const auto &assignment = std::get<ast::AssignExpr>(expression(2).data);
  const auto &product = std::get<ast::BinaryExpr>(assignment.value->data);
  assert(product.op == ast::BinaryOp::Mul && ast::get_if<ast::ParenExpr>(*product.left));
  assert(crate.source.slice(product.operator_span) == "*");
  const auto &array_binding = std::get<ast::LetStmt>(statements[3]->data);
  const auto &repeat = std::get<ast::RepeatArrayExpr>(array_binding.initializer->data);
  assert(ast::get_if<ast::ParenExpr>(*repeat.count));
  const auto &index = std::get<ast::IndexExpr>(expression(4).data);
  const auto &field = std::get<ast::FieldExpr>(index.base->data);
  assert(field.field.text == "bar");
  const auto &method = std::get<ast::MethodCallExpr>(field.base->data);
  assert(method.method.text == "foo" && method.arguments.size() == 1 && method.generic_args);
  const auto &borrow = std::get<ast::BorrowExpr>(expression(5).data);
  assert(borrow.mutability == ast::Mutability::Immutable);
  assert(std::get<ast::BorrowExpr>(borrow.operand->data).mutability == ast::Mutability::Mutable);
  assert(crate.source.slice(borrow.operator_span) == "&");
  const auto &return_expression = std::get<ast::ReturnExpr>(expression(6).data);
  assert(ast::get_if<ast::UnitExpr>(**return_expression.value));
  rx::DiagnosticEngine repeat_diagnostics;
  auto reparsed = rx::parse_source(source, repeat_diagnostics);
  assert(reparsed.syntax_ok && ast::dump(crate) == ast::dump(*reparsed.crate));
  std::set<ast::NodeId> identities;
  ast::walk(crate, [&](const ast::Node &node, const auto &) {
    assert(node.id.value != 0 && identities.insert(node.id).second);
    assert(node.span.begin <= node.span.end && node.span.end <= source.size());
  });
  assert(identities.size() > 60);

  const ast::SourceFile utf8{"中文\n1 + 2"};
  const auto plus = utf8.from_character_span({5, 6, 2, 2});
  assert(utf8.slice(plus) == "+");
  assert(plus.begin == 9 && plus.end == 10 && plus.file == utf8.id);
  assert(utf8.position(plus.begin) == std::make_pair(plus.line, plus.column));
  rx::DiagnosticEngine non_ascii_diagnostics;
  auto non_ascii = rx::parse_source("// 中文\nfn main() {}", non_ascii_diagnostics);
  assert(!non_ascii.syntax_ok && !non_ascii.crate && non_ascii_diagnostics.has_errors());
}
