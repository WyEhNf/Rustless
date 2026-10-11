#include "rx/ast/visitor.hpp"
#include "rx/frontend.hpp"
#include "rx/semantic.hpp"
#include "rx/semantic_literal.hpp"

#undef NDEBUG
#include <cassert>
#include <set>

int main() {
  namespace ast = rx::ast;
  assert(rx::integer_magnitude(ast::IntegerLiteral::parse("0b1_010u32")) == 10);
  assert(rx::integer_magnitude(ast::IntegerLiteral::parse("0o17usize")) == 15);
  assert(rx::integer_magnitude(ast::IntegerLiteral::parse("0xffi32")) == 255);
  assert(!rx::integer_magnitude({"0xff", ast::IntegerBase::Decimal, {}}));
  rx::DiagnosticEngine diagnostics;
  auto parsed = rx::parse_source(R"(
const N: usize = 0x2usize;
struct Pair { value: i32 }
impl Pair { fn get(&self) -> i32 { self.value } }
fn main() {
  let mut p = Pair { value: 1 };
  p.value += 2;
  let a: [i32; N] = [p.get(), 3];
  printInt(a[0]);
  if true { printInt(1); } else if false { printInt(2); }
}
)", diagnostics);
  assert(parsed.syntax_ok && parsed.crate);
  auto checked = rx::analyze(*parsed.crate, diagnostics);
  assert(!diagnostics.has_errors());
  std::set<rx::SymbolId> symbols;
  std::set<ast::NodeId> declarations;
  bool independent_id = false;
  for (const auto &symbol : checked.symbols) {
    assert(symbol.id.value && symbols.insert(symbol.id).second);
    assert(declarations.insert(symbol.declaration).second);
    independent_id |= symbol.id.value != symbol.declaration.value;
    assert(!rx::symbol_kind_name(symbol.kind).empty());
  }
  assert(independent_id);
  for (const auto &fact : checked.expressions)
    if (fact.symbol)
      assert(symbols.contains(*fact.symbol));
}
