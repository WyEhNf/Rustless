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

void reference_operand_adjustments() {
  Program program(R"(
fn main() {
  let x: i32 = 1;
  let r = &x;
  let rr = &r;
  let arithmetic = r + r;
  let ordering = rr < rr;
}
)");
  program.accepted();
  rx::SemanticIndex index(*program.parsed.crate, program.checked);
  ir::SemanticBridge bridge(index);
  for (const auto text : {"r + r", "rr < rr"}) {
    const auto &node = expression(program, text);
    const auto &payload = *ast::get_if<ast::BinaryExpr>(node);
    const auto &resolved = *index.expression(node).binary;
    const std::size_t count = text == std::string_view("r + r") ? 1 : 2;
    for (const auto *operand : {&resolved.left, &resolved.right}) {
      auto current = index.expression(operand->node).type;
      assert(current->kind() == K::Ref);
      assert(operand->adjustments.size() == count);
      for (const auto &adjustment : operand->adjustments) {
        assert(adjustment.kind == rx::Adjustment::Kind::Dereference);
        assert(current->kind() == K::Ref);
        assert(rx::same_type(adjustment.target, current->element()));
        current = adjustment.target;
      }
      assert(current->kind() == K::I32);
      assert(rx::same_type(current, operand->effective_type));
    }
    assert(resolved.left.node == payload.left->id &&
           resolved.right.node == payload.right->id);
    const auto lowered = bridge.binary(node, payload);
    assert(lowered.left.adjustments.size() == count);
    assert(lowered.right.adjustments.size() == count);
    if (payload.op == ast::BinaryOp::Add)
      assert(std::get<ir::ScalarArithmetic>(lowered.operation).op ==
             ir::BinaryOp::Add);
    else
      assert(std::get<ir::ScalarComparison>(lowered.operation).predicate ==
             ir::Predicate::SLt);
  }
}

void box_equality() {
  Program program(R"(
fn main() {
  let a = Box::<i32>::new(1);
  let b = Box::<i32>::new(1);
  let equal = a == b;
  let different = a != b;
}
)");
  program.accepted();
  rx::SemanticIndex index(*program.parsed.crate, program.checked);
  ir::SemanticBridge bridge(index);
  for (const auto text : {"a == b", "a != b"}) {
    const auto &node = expression(program, text);
    const auto &resolved = *index.expression(node).binary;
    assert(std::holds_alternative<rx::StructuralEquality>(resolved.operation));
    assert(resolved.left.effective_type->kind() == K::Box);
    const auto lowered =
        bridge.binary(node, *ast::get_if<ast::BinaryExpr>(node));
    const auto &operation = std::get<ir::StructuralEquality>(lowered.operation);
    assert(operation.compared_type->kind() == K::Box);
    assert(operation.negated == (text == std::string_view("a != b")));
    assert(lowered.result_type == ir::Type::I1);
  }
}

void callable_aliases() {
  Program program(R"(
fn f(x: i32) -> i32 { x + 1 }
fn main() {
  let g = f;
  let result = g(2);
  let out = printInt;
  out(result);
  let new_vec = Vec::<i32>::new;
  let values = new_vec();
}
)");
  program.accepted();
  rx::SemanticIndex index(*program.parsed.crate, program.checked);
  const auto &function =
      std::get<ast::Function>(program.parsed.crate->items[0]);
  const auto function_symbol = index.declaration(function.id).id;
  const auto &call = expression(program, "g(2)");
  const auto &callee = *ast::get_if<ast::CallExpr>(call)->callee;
  const auto &callee_fact = index.expression(callee);
  assert(callee_fact.symbol && *callee_fact.symbol != function_symbol);
  assert(index.symbol(*callee_fact.symbol).kind == rx::SymbolKind::Local);
  assert(std::get<rx::FunctionTarget>(*callee_fact.callable).symbol ==
         function_symbol);
  const auto &call_fact = index.expression(call);
  assert(call_fact.type->kind() == K::I32);
  assert(std::get<rx::FunctionTarget>(*call_fact.callable).symbol ==
         function_symbol);
  assert(call_fact.symbol == function_symbol);
  const auto &io_call = expression(program, "out(result)");
  const auto &io_callee = *ast::get_if<ast::CallExpr>(io_call)->callee;
  const auto &io_fact = index.expression(io_call);
  assert(io_fact.type->kind() == K::Unit);
  assert(std::get<rx::BuiltinTarget>(*io_fact.callable).builtin ==
         rx::BuiltinId::PrintInt);
  assert(rx::get_if<rx::BuiltinFunctionType>(*index.expression(io_callee).type)
             ->builtin == rx::BuiltinId::PrintInt);
  const auto &vec_call = expression(program, "new_vec()");
  const auto &vec_callee = *ast::get_if<ast::CallExpr>(vec_call)->callee;
  const auto &vec_fact = index.expression(vec_call);
  const auto &builtin =
      *rx::get_if<rx::BuiltinFunctionType>(*index.expression(vec_callee).type);
  assert(builtin.builtin == rx::BuiltinId::VecNew && builtin.owner);
  assert(builtin.owner->kind() == K::Vec &&
         builtin.owner->element()->kind() == K::I32);
  assert(rx::same_type(vec_fact.type, builtin.owner));
  assert(std::get<rx::BuiltinTarget>(*vec_fact.callable).builtin ==
         rx::BuiltinId::VecNew);
}

int main() {
  boolean_assignments();
  reference_operand_adjustments();
  box_equality();
  callable_aliases();
}
