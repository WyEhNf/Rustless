#include "rx/ast/visitor.hpp"
#include "rx/ast/frontend.hpp"
#include "rx/llvm/semantic.hpp"

#undef NDEBUG
#include <cassert>

int main() {
  rx::DiagnosticEngine diagnostics;
  auto parsed = rx::parse_source(R"(
struct P { value: i32 }
impl P { fn get(&self) -> i32 { self.value } }
fn read(value: &i32) -> i32 { *value }
fn copy(value: P) -> P { value }
fn main() {
  let mut x = 1;
  let shared = &mut x;
  let alias = read;
  let n = alias((shared));
  let p = P { value: n };
  let q = copy(p);
  let result = q.get();
  let output = printInt;
  output(result);
}
)",
                                 diagnostics);
  assert(parsed.syntax_ok && parsed.crate);
  auto checked = rx::analyze(*parsed.crate, diagnostics);
  assert(checked.success() && !diagnostics.has_errors());
  rx::ir::SemanticFacts index(checked.data());
  rx::ir::SemanticBridge bridge(checked.data());
  unsigned calls = 0;
  rx::ast::walk(*parsed.crate, [&](const auto &node, const auto &payload) {
    using PayloadType = std::decay_t<decltype(payload)>;
    if constexpr (std::is_same_v<PayloadType, rx::ast::CallExpr> ||
                  std::is_same_v<PayloadType, rx::ast::MethodCallExpr>) {
      const auto plan = bridge.call(node, payload);
      const auto spelling = parsed.crate->source.slice(node.span);
      assert(rx::same_type(plan.result, index.expression(node).type));
      if (spelling == "alias((shared))") {
        const auto &function = std::get<rx::ir::FunctionCall>(plan.target);
        assert(function.signature.name == "read");
        assert(function.signature.symbol == index.expression(node).symbol);
        assert(plan.arguments[0].adjustments.size() == 1);
        assert(plan.arguments[0].adjustments[0].kind == rx::Adjustment::Kind::MutToShared);
        assert(!plan.arguments[0].effective_type->is_mutable());
      } else if (spelling == "copy(p)") {
        const auto &function = std::get<rx::ir::FunctionCall>(plan.target);
        assert(function.signature.result.passing == rx::ir::Passing::Indirect);
        assert(function.signature.parameters[0].value.passing == rx::ir::Passing::Indirect);
      } else if (spelling == "q.get()") {
        const auto &function = std::get<rx::ir::FunctionCall>(plan.target);
        assert(function.signature.name == "P::get" && plan.receiver);
        assert(plan.receiver->adjustments.size() == 1);
        assert(plan.receiver->adjustments[0].kind == rx::Adjustment::Kind::BorrowShared);
      } else {
        assert(std::get<rx::ir::BuiltinCall>(plan.target).builtin == rx::BuiltinId::PrintInt);
      }
      ++calls;
    }
  });
  assert(calls == 4);
}
