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
  assert(structure.name == "S" && structure.fields.size() == 2);
  const auto &array_type = structure.fields[1].type;
  assert(array_type->kind == rx::ast::Type::Kind::Array);
  assert(array_type->array_length == "2");
  assert(array_type->element->path == std::vector<std::string>{"u32"});

  const auto &constant = std::get<rx::ast::Constant>(parsed.crate->items[1]);
  assert(constant.name == "N" && constant.value->text == "2");

  const auto &function = std::get<rx::ast::Function>(parsed.crate->items[2]);
  assert(function.parameters.size() == 1);
  assert(function.parameters[0].type->kind == rx::ast::Type::Kind::Reference);
  assert(function.parameters[0].type->is_mutable);
  assert(function.body->kind == rx::ast::Expr::Kind::Block);
  assert(function.body->statements.size() == 2);
  const auto &binding = function.body->statements[0];
  assert(binding->kind == rx::ast::Stmt::Kind::Let);
  assert(binding->annotation->kind == rx::ast::Type::Kind::Array);
  assert(binding->expression->kind == rx::ast::Expr::Kind::Array);
  const auto &tail = function.body->statements[1];
  assert(!tail->has_semicolon && tail->expression->kind == rx::ast::Expr::Kind::If);
  const auto &then_block = tail->expression->operands[1];
  const auto &sum = then_block->statements[0]->expression;
  assert(sum->kind == rx::ast::Expr::Kind::Binary && sum->text == "+");
  assert(sum->operands[1]->kind == rx::ast::Expr::Kind::Binary);
  assert(sum->operands[1]->text == "*");
  assert(sum->span.begin < sum->span.end && sum->id != 0);

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
  assert(std::get<rx::ast::Use>(more.crate->items[0]).text == "crate::io");
  const auto &pair = std::get<rx::ast::Struct>(more.crate->items[1]);
  assert(pair.derives.size() == 2 && pair.derives[0] == "Copy");
  const auto &implementation = std::get<rx::ast::Impl>(more.crate->items[2]);
  assert(implementation.constants.size() == 1);
  assert(implementation.functions.size() == 1);
  assert(implementation.functions[0].has_self);
  const auto &run = std::get<rx::ast::Function>(more.crate->items[3]);
  const auto &outer_ref = run.parameters[0].type;
  assert(outer_ref->kind == rx::ast::Type::Kind::Reference);
  assert(!outer_ref->is_mutable && outer_ref->element->is_mutable);
  const auto &run_statements = run.body->statements;
  assert(run_statements.size() == 4);
  assert(run_statements[0]->expression->kind == rx::ast::Expr::Kind::Struct);
  assert(run_statements[1]->expression->kind == rx::ast::Expr::Kind::Loop);
  assert(run_statements[2]->expression->kind == rx::ast::Expr::Kind::While);
  const auto &cast = run_statements[3]->expression;
  assert(cast->kind == rx::ast::Expr::Kind::Cast);
  assert(cast->operands[0]->kind == rx::ast::Expr::Kind::MethodCall);

  rx::DiagnosticEngine bad_diagnostics;
  auto bad = rx::parse_source("fn broken( {}", bad_diagnostics);
  assert(!bad.syntax_ok && !bad.crate && bad_diagnostics.has_errors());
}
