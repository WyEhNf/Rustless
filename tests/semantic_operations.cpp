#include "rx/ast/visitor.hpp"
#include "rx/frontend.hpp"
#include "rx/ir_semantic.hpp"

#undef NDEBUG
#include <array>
#include <cassert>
#include <iostream>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

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

void shift_policy() {
  Program program(R"(
fn main() {
  let mut signed: i32 = -8;
  let unsigned: u32 = 8;
  let a = signed >> 32;
  let b = unsigned >> 32;
  let c = unsigned << 32;
  signed >>= 32;
}
)");
  program.accepted();
  rx::SemanticIndex index(*program.parsed.crate, program.checked);
  ir::SemanticBridge bridge(index);
  const auto expected =
      std::array{std::pair{"signed >> 32", ir::BinaryOp::AShr},
                 std::pair{"unsigned >> 32", ir::BinaryOp::LShr},
                 std::pair{"unsigned << 32", ir::BinaryOp::Shl}};
  for (const auto &[text, op] : expected) {
    const auto &node = expression(program, text);
    const auto &fact = index.expression(node);
    const auto &resolved = std::get<rx::ShiftOperation>(fact.binary->operation);
    assert(resolved.count_policy == rx::ShiftCountPolicy::MaskToWidth);
    const auto lowered =
        bridge.binary(node, *ast::get_if<ast::BinaryExpr>(node));
    const auto &operation = std::get<ir::ScalarArithmetic>(lowered.operation);
    assert(operation.op == op && operation.operand_type == ir::Type::I32);
    assert(operation.shift_count_mask == 31);
  }
  const auto &assignment = expression(program, "signed >>= 32");
  const auto &fact = index.expression(assignment);
  assert(std::holds_alternative<rx::ShiftOperation>(fact.compound->operation));
  const auto lowered = bridge.compound(
      assignment, *ast::get_if<ast::CompoundAssignExpr>(assignment));
  assert(lowered.operation.op == ir::BinaryOp::AShr);
  assert(lowered.operation.shift_count_mask == 31);
}

void projection_result_operations() {
  Program program(R"(
struct P { value: i32 }
impl P { fn get(&self) -> i32 { self.value } }
fn main() {
  let p = P { value: 1 };
  let r = &p;
  let array = [1, 2];
  let ar = &array;
  let field_result = r.value + 1;
  let index_result = ar[0] + 1;
  let method_result = r.get() + 1;
}
)");
  program.accepted();
  rx::SemanticIndex index(*program.parsed.crate, program.checked);
  ir::SemanticBridge bridge(index);
  for (const auto text : {"r.value + 1", "ar[0] + 1", "r.get() + 1"}) {
    const auto &node = expression(program, text);
    const auto &fact = index.expression(node);
    assert(fact.binary->left.effective_type->kind() == K::I32);
    assert(fact.binary->left.adjustments.empty());
    const auto &left = index.expression(fact.binary->left.node);
    assert(left.type->kind() == K::I32);
    assert(left.receiver && left.adjustments.empty());
    assert(!left.receiver->adjustments.empty());
    assert(left.receiver->adjustments.front().kind ==
           rx::Adjustment::Kind::Dereference);
    const auto lowered =
        bridge.binary(node, *ast::get_if<ast::BinaryExpr>(node));
    assert(std::get<ir::ScalarArithmetic>(lowered.operation).op ==
           ir::BinaryOp::Add);
    assert(lowered.result_type == ir::Type::I32);
  }
}

void unary_operand_adjustments() {
  Program rejected("fn main() { let x = 1; let rr = &&x; let y = !rr; }");
  assert(rejected.diagnostics.has_errors() && !rejected.checked.success());
  Program nested("fn main() { let x = 1; let y = &&x + &x; }");
  assert(nested.diagnostics.has_errors() && !nested.checked.success());
  Program program(R"(
fn main() {
  let value: i32 = 1;
  let r = &value;
  let rr = &r;
  let negative = -r;
  let complemented = !r;
  let flag = true;
  let br = &flag;
  let not_flag = !br;
  let explicit = *r;
}
)");
  program.accepted();
  rx::SemanticIndex index(*program.parsed.crate, program.checked);
  for (const auto text : {"-r", "!r", "!br"}) {
    const auto &node = expression(program, text);
    const auto &payload = *ast::get_if<ast::UnaryExpr>(node);
    const auto &fact = index.expression(node);
    assert(fact.unary && fact.unary->operand.node == payload.operand->id);
    const auto &operand = fact.unary->operand;
    auto current = index.expression(*payload.operand).type;
    const std::size_t count = 1;
    assert(operand.adjustments.size() == count);
    for (const auto &adjustment : operand.adjustments) {
      assert(current->kind() == K::Ref);
      assert(adjustment.kind == rx::Adjustment::Kind::Dereference);
      assert(rx::same_type(adjustment.target, current->element()));
      current = adjustment.target;
    }
    assert(rx::same_type(current, operand.effective_type));
    assert(rx::same_type(current, fact.unary->result));
    assert(fact.type->kind() ==
           (text == std::string_view("!br") ? K::Bool : K::I32));
    assert(fact.unary->operation == (text == std::string_view("-r")
                                         ? rx::UnaryOperation::Negate
                                         : rx::UnaryOperation::Complement));
  }
  const auto &explicit_fact = index.expression(expression(program, "*r"));
  assert(explicit_fact.unary->operation == rx::UnaryOperation::Dereference);
  assert(explicit_fact.unary->operand.adjustments.empty());
  assert(explicit_fact.unary->operand.effective_type->kind() == K::Ref);
  assert(explicit_fact.type->kind() == K::I32 && explicit_fact.is_place);
}

void parenthesized_operand_coercion() {
  Program program(R"(
fn main() {
  let mut x: i32 = 1;
  let shared = &x;
  let mutable = &mut x;
  let plain = shared + mutable;
  let parenthesized = shared + (mutable);
}
)");
  program.accepted();
  rx::SemanticIndex index(*program.parsed.crate, program.checked);
  ir::SemanticBridge bridge(index);
  for (const auto text : {"shared + mutable", "shared + (mutable)"}) {
    const auto &node = expression(program, text);
    const auto &payload = *ast::get_if<ast::BinaryExpr>(node);
    const auto &right = index.expression(node).binary->right;
    assert(right.adjustments.size() == 2);
    assert(right.adjustments[0].kind == rx::Adjustment::Kind::MutToShared);
    assert(right.adjustments[0].target->kind() == K::Ref &&
           !right.adjustments[0].target->is_mutable());
    assert(right.adjustments[1].kind == rx::Adjustment::Kind::Dereference);
    assert(right.effective_type->kind() == K::I32);
    const auto lowered = bridge.binary(node, payload);
    assert(std::get<ir::ScalarArithmetic>(lowered.operation).op ==
           ir::BinaryOp::Add);
  }
}

void language_boundaries() {
  for (const auto source : {
      "fn f() {} fn main() { f::<i32>(); }",
      "fn f(self) {} fn main() {}",
      "fn main() { let b = Box::<i32>::new(1); let n = Box::<i32>::len(&b); }"}) {
    Program program(source);
    assert(program.diagnostics.has_errors() && !program.checked.success());
  }
  Program ordering(R"(
fn main() {
  let mut x = 1;
  let mut y = 2;
  let a = &x < &mut y;
  let b = &&x < &&y;
  let c = &mut x > &mut y;
}
)");
  ordering.accepted();
}

int main() {
  language_boundaries();
  boolean_assignments();
  reference_operand_adjustments();
  box_equality();
  callable_aliases();
  shift_policy();
  projection_result_operations();
  unary_operand_adjustments();
  parenthesized_operand_coercion();
}
