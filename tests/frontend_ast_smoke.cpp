#include "rx/frontend.hpp"

#undef NDEBUG
#include <cassert>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

int main() {
  constexpr std::string_view source = R"(
struct S { value: i32, data: [u32; 2] }
const N: usize = 2;
fn f(x: &mut S) -> i32 {
    let a: [i32; 2] = [1, 2];
    if true { x.value + a[0] * 3 } else { 0 }
}
)";
  rx::DiagnosticEngine diagnostics;
  auto parsed = rx::parse_source(source, diagnostics);
  assert(parsed.syntax_ok && parsed.crate && !diagnostics.has_errors());
  assert(parsed.crate->items.size() == 3);

  const auto &structure = std::get<rx::ast::Struct>(parsed.crate->items[0]);
  assert(structure.name.text == "S" && structure.fields.size() == 2);
  const auto &array_type = structure.fields[1].type;
  assert(rx::ast::get_if<rx::ast::ArrayType>(*array_type));
  assert(std::get<rx::ast::IntegerExpr>(std::get<rx::ast::ArrayType>(array_type->data).length->data).literal.spelling == "2");
  assert(rx::ast::path_name(std::get<rx::ast::PathType>(std::get<rx::ast::ArrayType>(array_type->data).element->data).path) == "u32");

  const auto &constant = std::get<rx::ast::Constant>(parsed.crate->items[1]);
  assert(constant.name.text == "N" && std::get<rx::ast::IntegerExpr>(constant.value->data).literal.spelling == "2");

  const auto &function = std::get<rx::ast::Function>(parsed.crate->items[2]);
  assert(function.signature.parameters.size() == 1);
  const auto &reference = std::get<rx::ast::ReferenceType>(function.signature.parameters[0].type->data);
  assert(reference.mutability == rx::ast::Mutability::Mutable);
  assert(function.body->statements.size() == 1 && function.body->tail);
  const auto &binding = std::get<rx::ast::LetStmt>(function.body->statements[0]->data);
  assert(binding.annotation && rx::ast::get_if<rx::ast::ArrayType>(**binding.annotation));
  assert(rx::ast::get_if<rx::ast::ArrayExpr>(*binding.initializer));
  const auto &conditional = std::get<rx::ast::IfExpr>((*function.body->tail)->data);
  const auto &sum = std::get<rx::ast::BinaryExpr>((*conditional.then_block->tail)->data);
  assert(sum.op == rx::ast::BinaryOp::Add);
  assert(std::get<rx::ast::BinaryExpr>(sum.right->data).op == rx::ast::BinaryOp::Mul);
  assert(sum.left->span.begin < sum.left->span.end && sum.left->id.value != 0);

  constexpr std::string_view more_source = R"(
use crate::io;
#[derive(Copy, Clone)]
struct Pair { left: i32 }
impl Pair {
    const ZERO: i32 = 0;
    fn get(&self) -> i32 { self.left }
}
fn run(x: &&mut i32) {
    let p: Pair = Pair { left: 1 };
    loop { break; }
    while false { continue; }
    p.get() as i32;
}
)";
  rx::DiagnosticEngine more_diagnostics;
  auto more = rx::parse_source(more_source, more_diagnostics);
  assert(more.syntax_ok && more.crate && !more_diagnostics.has_errors());
  assert(more.crate->items.size() == 4);
  const auto &use = std::get<rx::ast::Use>(more.crate->items[0]);
  assert(use.tree.prefix.segments.size() == 2);
  assert(rx::ast::name_text(use.tree.prefix.segments[1]) == "io");
  const auto &pair = std::get<rx::ast::Struct>(more.crate->items[1]);
  assert(pair.attributes[0].names.size() == 2);
  assert(pair.attributes[0].names[0].kind == rx::ast::DeriveKind::Copy);
  const auto &implementation = std::get<rx::ast::Impl>(more.crate->items[2]);
  assert(implementation.members.size() == 2);
  assert(std::holds_alternative<rx::ast::Constant>(implementation.members[0]));
  assert(std::get<rx::ast::Function>(implementation.members[1]).signature.receiver);
  const auto &run = std::get<rx::ast::Function>(more.crate->items[3]);
  const auto &outer = std::get<rx::ast::ReferenceType>(run.signature.parameters[0].type->data);
  assert(outer.mutability == rx::ast::Mutability::Immutable);
  assert(std::get<rx::ast::ReferenceType>(outer.referent->data).mutability == rx::ast::Mutability::Mutable);
  const auto &statements = run.body->statements;
  assert(statements.size() == 4 && !run.body->tail);
  assert(rx::ast::get_if<rx::ast::StructExpr>(*std::get<rx::ast::LetStmt>(statements[0]->data).initializer));
  assert(rx::ast::get_if<rx::ast::LoopExpr>(*std::get<rx::ast::ExprStmt>(statements[1]->data).expression));
  assert(rx::ast::get_if<rx::ast::WhileExpr>(*std::get<rx::ast::ExprStmt>(statements[2]->data).expression));
  const auto &cast = std::get<rx::ast::CastExpr>(std::get<rx::ast::ExprStmt>(statements[3]->data).expression->data);
  assert(rx::ast::get_if<rx::ast::MethodCallExpr>(*cast.value));

  rx::DiagnosticEngine bad_diagnostics;
  auto bad = rx::parse_source("fn broken( {}", bad_diagnostics);
  assert(!bad.syntax_ok && !bad.crate && bad_diagnostics.has_errors());
}
