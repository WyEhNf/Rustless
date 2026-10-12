#include "rx/frontend.hpp"
#include "rx/semantic.hpp"

#undef NDEBUG
#include <cassert>
#include <string_view>
#include <type_traits>

static_assert(!std::is_same_v<rx::SymbolId, rx::ast::NodeId>);
static_assert(!std::is_convertible_v<rx::ast::NodeId, rx::SymbolId>);

int main() {
  constexpr std::string_view accepted = R"(
const N: usize = 2;
struct Pair { value: i32 }
fn main() {
    let mut pair: Pair = Pair { value: 1 };
    pair.value = 2;
    let values: [i32; N] = [pair.value, 3];
    printInt(values[0]);
}
)";
  rx::DiagnosticEngine valid_diagnostics;
  auto valid_ast = rx::parse_source(accepted, valid_diagnostics);
  assert(valid_ast.syntax_ok && valid_ast.crate);
  auto valid = rx::analyze(*valid_ast.crate, valid_diagnostics);
  assert(!valid_diagnostics.has_errors());
  assert(valid.data().summary.functions == 1);
  assert(valid.data().summary.structures == 1);
  assert(valid.data().summary.constants == 1);
  assert(!valid.data().symbols.empty());
  for (const auto &symbol : valid.data().symbols)
    assert(symbol.declaration.value != 0);
  assert(!valid.data().expressions.empty());
  assert(!valid.data().resolved_types.empty());
  assert(valid.data().constants.size() == 1 && valid.data().constants[0].bits == 2);

  constexpr std::string_view wrong_type = R"(
fn main() { let value: i32 = true; }
)";
  rx::DiagnosticEngine type_diagnostics;
  auto type_ast = rx::parse_source(wrong_type, type_diagnostics);
  assert(type_ast.syntax_ok && type_ast.crate);
  rx::analyze(*type_ast.crate, type_diagnostics);
  assert(type_diagnostics.has_errors());

  constexpr std::string_view immutable_field = R"(
struct Pair { value: i32 }
fn main() {
    let pair: Pair = Pair { value: 1 };
    pair.value = 2;
}
)";
  rx::DiagnosticEngine field_diagnostics;
  auto field_ast = rx::parse_source(immutable_field, field_diagnostics);
  assert(field_ast.syntax_ok && field_ast.crate);
  rx::analyze(*field_ast.crate, field_diagnostics);
  assert(field_diagnostics.has_errors());

  rx::DiagnosticEngine paren_diagnostics;
  auto paren_ast = rx::parse_source(
      "fn main() { let mut x = 1; let shared: &i32 = ((&mut x)); }",
      paren_diagnostics);
  auto paren_semantics = rx::analyze(*paren_ast.crate, paren_diagnostics);
  assert(!paren_diagnostics.has_errors());
  const auto &main = std::get<rx::ast::Function>(paren_ast.crate->items[0]);
  const auto &initializer =
      std::get<rx::ast::LetStmt>(main.body->statements[1]->data).initializer;
  const auto &borrow = rx::ast::unparen(initializer);
  for (const auto &fact : paren_semantics.data().expressions) {
    if (fact.node == initializer->id)
      assert(fact.adjustments.empty());
    if (fact.node == borrow->id) {
      assert(fact.adjustments.size() == 1);
      assert(fact.adjustments[0].kind == rx::Adjustment::Kind::MutToShared);
    }
  }
}
