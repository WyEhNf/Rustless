#include "rx/ast/visitor.hpp"
#include "rx/ast/frontend.hpp"
#include "rx/semantic/semantic.hpp"
#include "rx/semantic/safety/safe_gate.hpp"
#include "rx/semantic/model/literal.hpp"

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
)",
                                 diagnostics);
  assert(parsed.syntax_ok && parsed.crate);
  auto checked = rx::analyze(*parsed.crate, diagnostics);
  const auto &facts = checked.data();
  assert(!diagnostics.has_errors());
  assert(checked.success() && facts.structures.size() == 1 &&
         facts.functions.size() == 2);
  assert(facts.structures[0].fields.size() == 1);
  assert(!facts.symbol_types.empty());
  bool saw_receiver = false;
  for (const auto &function : facts.functions) {
    assert(function.result);
    if (function.receiver) {
      saw_receiver = true;
      assert(function.receiver->type->kind() == rx::SemanticType::Kind::Ref);
    }
  }
  assert(saw_receiver);
  rx::SafeGate index(*parsed.crate, checked);
  const auto &pair = std::get<ast::Struct>(parsed.crate->items[1]);
  assert(index.declaration(pair.id).kind == rx::SymbolKind::Struct);
  assert(index.structure(index.declaration(pair.id).id).fields[0] ==
         index.declaration(pair.fields[0].id).id);
  assert(index.type(*pair.fields[0].type)->kind() ==
         rx::SemanticType::Kind::I32);
  auto missing = facts;
  missing.expressions.pop_back();
  bool rejected = false;
  try {
    (void)rx::SemanticResult::publish(*parsed.crate, std::move(missing));
  } catch (const std::exception &) {
    rejected = true;
  }
  assert(rejected);
  std::set<rx::SymbolId> symbols;
  std::set<ast::NodeId> declarations;
  bool independent_id = false;
  for (const auto &symbol : facts.symbols) {
    assert(symbol.id.value && symbols.insert(symbol.id).second);
    assert(declarations.insert(symbol.declaration).second);
    independent_id |= symbol.id.value != symbol.declaration.value;
    assert(!rx::symbol_kind_name(symbol.kind).empty());
  }
  assert(independent_id);
  for (const auto &fact : facts.expressions)
    if (fact.symbol)
      assert(symbols.find(*fact.symbol) != symbols.end());
}
