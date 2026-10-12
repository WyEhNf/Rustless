#include "rx/ast/visitor.hpp"
#include "rx/frontend.hpp"
#include "rx/ir_semantic.hpp"

#undef NDEBUG
#include <cassert>
#include <iostream>
#include <string_view>

namespace ast = rx::ast;
namespace ir = rx::ir;
using K = rx::SemanticType::Kind;

struct Program {
  rx::DiagnosticEngine diagnostics;
  rx::FrontendResult parsed;
  rx::SemanticResult checked;
  explicit Program(std::string_view source) {
    parsed = rx::parse_source(source, diagnostics);
    assert(parsed.syntax_ok && parsed.crate);
    checked = rx::analyze(*parsed.crate, diagnostics);
  }
  void accepted() const {
    diagnostics.print(std::cerr);
    assert(!diagnostics.has_errors() && checked.success());
  }
};

const ast::Expr &expression(const Program &program, std::string_view text) {
  const ast::Expr *found = nullptr;
  ast::walk(*program.parsed.crate, [&](const auto &owner, const auto &) {
    using Owner = std::remove_cvref_t<decltype(owner)>;
    if constexpr (std::same_as<Owner, ast::Expr>)
      if (program.parsed.crate->source.slice(owner.span) == text) {
        assert(!found);
        found = &owner;
      }
  });
  assert(found);
  return *found;
}

void boolean_assignments() {
  for (const auto *operation : {"+=", "<<="}) {
    const auto source = std::string("fn main() { let mut b = true; b ") +
                        operation + " false; }";
    Program rejected(source);
    assert(rejected.diagnostics.has_errors() && !rejected.checked.success());
  }
  Program program(R"(
fn main() {
  let mut b = true;
  b &= false;
  b |= true;
  b ^= false;
}
)");
  program.accepted();
  rx::SemanticIndex index(*program.parsed.crate, program.checked);
  ir::SemanticBridge bridge(index);
  for (const auto text : {"b &= false", "b |= true", "b ^= false"}) {
    const auto &node = expression(program, text);
    const auto &fact = index.expression(node);
    assert(fact.type->kind() == K::Unit && fact.compound);
    assert(
        std::holds_alternative<rx::BitwiseOperation>(fact.compound->operation));
    assert(fact.compound->left.effective_type->kind() == K::Bool);
    const auto lowered =
        bridge.compound(node, *ast::get_if<ast::CompoundAssignExpr>(node));
    assert(lowered.operation.operand_type == ir::Type::I1);
    assert(!lowered.operation.shift_count_mask);
  }
}

int main() { boolean_assignments(); }
