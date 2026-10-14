#include "rx/ast/frontend.hpp"
#include "rx/semantic/safety/safe_gate.hpp"
#include "rx/semantic/semantic.hpp"

#undef NDEBUG
#include <cassert>
#include <iostream>
#include <string_view>

namespace {
void check(std::string_view source, bool accepted) {
  rx::DiagnosticEngine diagnostics;
  auto parsed = rx::parse_source(source, diagnostics);
  assert(parsed.syntax_ok && parsed.crate);
  auto result = rx::analyze(*parsed.crate, diagnostics);
  if (result.success() != accepted) {
    std::cerr << source << '\n';
    diagnostics.print(std::cerr);
  }
  assert(result.success() == accepted);
  assert(diagnostics.has_errors() != accepted);
}

void check_tail_types() {
  rx::DiagnosticEngine diagnostics;
  auto parsed = rx::parse_source(
      "fn main() { let x: i32 = { { loop {}; 42 }; }; }", diagnostics);
  assert(parsed.syntax_ok && parsed.crate);
  auto result = rx::analyze(*parsed.crate, diagnostics);
  assert(result.success() && !diagnostics.has_errors());
  rx::SafeGate facts(*parsed.crate, result);
  const auto &function = std::get<rx::ast::Function>(parsed.crate->items[0]);
  const auto &binding =
      std::get<rx::ast::LetStmt>(function.body->statements[0]->data);
  const auto &outer = std::get<rx::ast::BlockExpr>(binding.initializer->data);
  const auto &statement =
      std::get<rx::ast::ExprStmt>(outer.block->statements[0]->data);
  const auto &inner = std::get<rx::ast::BlockExpr>(statement.expression->data);
  assert(facts.expression(*inner.block).type->kind() ==
         rx::SemanticType::Kind::I32);
  assert(facts.expression(*outer.block).type->kind() ==
         rx::SemanticType::Kind::Never);
  const auto &adjustments = facts.expression(*binding.initializer).adjustments;
  assert(adjustments.size() == 1 &&
         adjustments[0].kind == rx::Adjustment::Kind::NeverToAny);
}
} // namespace

int main() {
  // Official divergent-block-without-tail-statement regression.
  check(R"(
use rx::core::*;
fn main() {
  let x: i32 = { { loop {}; 42 }; };
  println_i32(x);
}
)",
        true);
  check("fn main() { let x: i32 = { (({ loop {}; 42 })); }; }", true);
  check("fn main() { let x: i32 = { let y = { loop {}; 42 }; }; }", true);
  check("fn f() -> i32 { loop {}; printInt(1); ; } fn main() {}", true);
  check("fn f() -> i32 { { return 7; 42 }; } fn main() {}", true);
  check(R"(
fn main() {
  let x: i32 = {
    if getInt() == 0 { loop {}; () } else { loop {}; () };
  };
}
)",
        true);
  check("fn main() { let x: i32 = { printInt({ loop {}; 1 }); }; }", true);
  check("fn main() { let x: i32 = { 1 + { loop {}; 42 }; }; }", true);
  // Unreachable breaks still contribute to the loop's static type.
  check("fn f() -> i32 { loop { return 7; break 1; }; } fn main() {}", true);
  check("fn main() { let x: i32 = { loop { loop { break; } }; }; }", true);

  // A normal path, short circuit, or while exit prevents unconditional divergence.
  check(R"(
fn main() {
  let x: i32 = { if getInt() == 0 { loop {}; () } else {}; };
}
)",
        false);
  check("fn main() { let x: i32 = { loop { break; }; }; }", false);
  check("fn main() { let x: i32 = { false && { loop {}; true }; }; }", false);
  check("fn main() { let x: i32 = { while false { loop {}; }; }; }", false);
  // Divergence must not hide tail mismatches or errors in unreachable statements.
  check("fn main() { let x: i32 = { loop {}; true }; }", false);
  check("fn main() { loop {}; missing; }", false);
  check("fn main() { loop {}; let bad: bool = 1; }", false);
  check("fn main() { loop {}; { 42 } printInt(1); }", false);
  check_tail_types();
}
