#include "rx/frontend.hpp"
#include "rx/semantic.hpp"

#undef NDEBUG
#include <cassert>
#include <string_view>

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
  assert(valid.summary.functions == 1);
  assert(valid.summary.structures == 1);
  assert(valid.summary.constants == 1);
  assert(!valid.symbols.empty());
  assert(!valid.expressions.empty());
  assert(!valid.resolved_types.empty());
  assert(valid.constants.size() == 1 && valid.constants[0].bits == 2);

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
}
