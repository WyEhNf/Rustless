#include "rx/ir_semantic.hpp"
#include "rx/ast/visitor.hpp"
#include "rx/frontend.hpp"

#undef NDEBUG
#include <cassert>
#include <iostream>
#include <type_traits>

namespace ast = rx::ast;
namespace ir = rx::ir;
static_assert(std::is_constructible_v<ir::SemanticBridge, const rx::SemanticIndex &>);
static_assert(!std::is_constructible_v<ir::SemanticBridge, rx::SemanticIndex &&>);
static_assert(!std::is_constructible_v<ir::SemanticBridge, const rx::SemanticIndex &&>);
namespace {
void check_unary_lowering() {
  rx::DiagnosticEngine diagnostics;
  auto parsed = rx::parse_source(R"(
#[derive(Clone, Copy)]
struct Payload { value: i32 }
fn main() {
  let x = 8;
  let flag = true;
  let negative = -&x;
  let inverted = !&x;
  let opposite = !&flag;
  let address = &x;
  let value = *address;
  let boxed = Box::<Payload>::new(Payload { value: 1 });
  let field = (*boxed).value;
}
)",
                                 diagnostics);
  assert(parsed.syntax_ok && parsed.crate);
  auto checked = rx::analyze(*parsed.crate, diagnostics);
  diagnostics.print(std::cerr);
  assert(!diagnostics.has_errors());
  rx::SemanticIndex index(*parsed.crate, checked);
  ir::SemanticBridge bridge(index);
  std::vector<ir::ScalarUnary> computations;
  std::vector<rx::SemanticType::Kind> place_types;
  unsigned converted_operands = 0;
  ast::walk(*parsed.crate, [&](const auto &owner, const auto &payload) {
    if constexpr (std::is_same_v<std::decay_t<decltype(payload)>, ast::UnaryExpr>) {
      const auto lowered = bridge.unary(owner, payload);
      assert(lowered.operand.node == payload.operand->id);
      assert(rx::same_type(lowered.result, index.expression(owner).type));
      if (const auto *scalar = std::get_if<ir::ScalarUnary>(&lowered.operation)) {
        computations.push_back(*scalar);
        converted_operands += !lowered.operand.adjustments.empty();
      } else {
        const auto &place = std::get<ir::DereferencePlace>(lowered.operation);
        place_types.push_back(place.element_type->kind());
        assert(index.expression(owner).is_place);
        assert(place.layout.bytes == 4 && place.layout.alignment == 4);
      }
    }
  });
  assert(computations.size() == 3 && converted_operands == 3);
  assert(computations[0].op == ir::BinaryOp::Sub && computations[0].left_constant == 0);
  assert(computations[1].op == ir::BinaryOp::Xor && computations[1].operand_type == ir::Type::I32 &&
         computations[1].left_constant == 0xffffffffu);
  assert(computations[2].op == ir::BinaryOp::Xor && computations[2].operand_type == ir::Type::I1 &&
         computations[2].left_constant == 1);
  using TypeKind = rx::SemanticType::Kind;
  assert(place_types == (std::vector{TypeKind::I32, TypeKind::Struct}));
}
void check_equality_and_adjustments() {
  rx::DiagnosticEngine diagnostics;
  auto parsed = rx::parse_source(R"(
#[derive(PartialEq, Eq)]
struct Cell { value: i32 }
fn main() {
  let a = Box::<i32>::new(1);
  let b = Box::<i32>::new(1);
  let box_equal = a == b;
  let box_different = a != b;
  let array_equal = [1, 2] == [1, 2];
  let cell_equal = Cell { value: 1 } == Cell { value: 1 };
  let x = 8;
  let sum = &x + &x;
  let order = &(&x) < &(&x);
  let mut flag = true;
  flag &= false;
}
)",
                                 diagnostics);
  assert(parsed.syntax_ok && parsed.crate);
  auto checked = rx::analyze(*parsed.crate, diagnostics);
  diagnostics.print(std::cerr);
  assert(!diagnostics.has_errors());
  rx::SemanticIndex index(*parsed.crate, checked);
  ir::SemanticBridge bridge(index);
  std::vector<rx::SemanticType::Kind> content_types;
  std::vector<bool> negated;
  unsigned converted_operands = 0;
  ast::walk(*parsed.crate, [&](const auto &owner, const auto &payload) {
    using PayloadType = std::decay_t<decltype(payload)>;
    if constexpr (std::is_same_v<PayloadType, ast::BinaryExpr>) {
      const auto lowered = bridge.binary(owner, payload);
      if (const auto *content = std::get_if<ir::StructuralEquality>(&lowered.operation)) {
        content_types.push_back(content->compared_type->kind());
        negated.push_back(content->negated);
        assert(lowered.result_type == ir::Type::I1);
      }
      for (const auto *use : {&lowered.left, &lowered.right}) {
        if (!use->adjustments.empty()) {
          ++converted_operands;
          assert(use->effective_type->kind() == rx::SemanticType::Kind::I32);
          for (const auto &adjustment : use->adjustments) {
            assert(adjustment.kind == rx::Adjustment::Kind::Dereference);
          }
        }
      }
    }
    if constexpr (std::is_same_v<PayloadType, ast::CompoundAssignExpr>) {
      const auto lowered = bridge.compound(owner, payload);
      assert(lowered.operation.op == ir::BinaryOp::And);
      assert(lowered.operation.operand_type == ir::Type::I1);
    }
  });
  using TypeKind = rx::SemanticType::Kind;
  assert(content_types ==
         (std::vector{TypeKind::Box, TypeKind::Box, TypeKind::Array, TypeKind::Struct}));
  assert(negated == (std::vector{false, true, false, false}));
  assert(converted_operands == 4);
}
}
int main() {
  check_unary_lowering();
  check_equality_and_adjustments();
  const auto target = ir::TargetLayout::rv32();
  assert(target.integer_bits() == 32 && target.shift_count_mask() == 31);
  assert(target.integer().bytes == 4 && target.integer().alignment == 4);
  assert(target.pointer().bytes == 4 && target.pointer().alignment == 4);
  assert(target.boolean().bytes == 1 && target.boolean().alignment == 1);
  assert(target.unit().bytes == 0 && target.unit().alignment == 1);
  const auto vector = target.vector();
  assert(vector.data_offset == 0 && vector.length_offset == 4 && vector.capacity_offset == 8);
  assert(vector.storage.bytes == 12 && vector.storage.alignment == 4);
  assert(vector.capacity_offset + target.integer().bytes == vector.storage.bytes);
  rx::DiagnosticEngine diagnostics;
  auto parsed = rx::parse_source(R"(
struct Packet { flag: bool, count: i32, bits: [bool; 3] }
impl Packet {
  const K: i32 = 1;
  fn get(&self) -> i32 { self.count }
  fn copy(value: Packet) -> Packet { value }
}
fn take(unit: (), value: Packet) -> Packet { value }
fn main() {
  let mut signed: i32 = 8;
  let mut unsigned: u32 = 8;
  let a = signed / 2;
  let b = unsigned / 2;
  let c = signed >> 1;
  let d = unsigned >> 1;
  let e = signed < 2;
  let f = unsigned < 2;
  let g = true && false;
  let h = true || false;
  signed >>= 1;
  unsigned /= 2;
}
)",
                                 diagnostics);
  assert(parsed.syntax_ok && parsed.crate);
  auto checked = rx::analyze(*parsed.crate, diagnostics);
  diagnostics.print(std::cerr);
  assert(!diagnostics.has_errors());
  rx::SemanticIndex index(*parsed.crate, checked);
  ir::SemanticBridge bridge(index);
  assert(bridge.target().llvm_data_layout() == target.llvm_data_layout());
  assert(bridge.target().triple() == target.triple());
  const auto &packet = std::get<ast::Struct>(parsed.crate->items[0]);
  const auto &layout = bridge.structure(index.declaration(packet.id).id);
  assert(layout.layout.bytes == 12 && layout.layout.alignment == 4);
  assert(layout.fields.size() == 3);
  assert(layout.fields[0].offset == 0 && layout.fields[1].offset == 4 &&
         layout.fields[2].offset == 8);
  assert(layout.fields[2].layout.bytes == 3);
  auto declarations = bridge.declarations();
  assert(declarations.size() == 4);
  assert(declarations[0].name == "Packet::get");
  assert(declarations[0].parameters == std::vector{ir::Type::Ptr});
  assert(declarations[0].result == ir::Type::I32);
  assert(declarations[1].name == "Packet::copy" && declarations[1].result == ir::Type::Void);
  assert(declarations[1].parameters == (std::vector{ir::Type::Ptr, ir::Type::Ptr}));
  assert(declarations[2].name == "take");
  assert(declarations[2].parameters == (std::vector{ir::Type::Ptr, ir::Type::Ptr}));
  for (const auto &declaration : declarations) {
    assert(declaration.symbol && declaration.origin);
    assert(index.symbol(*declaration.symbol).declaration == declaration.origin->node);
  }
  const auto &take = std::get<ast::Function>(parsed.crate->items[2]);
  auto abi = bridge.function(take);
  assert(abi.result.passing == ir::Passing::Indirect);
  assert(abi.parameters[0].value.type == ir::Type::Void);
  assert(abi.parameters[1].value.passing == ir::Passing::Indirect);
  auto foreign = rx::parse_source(parsed.crate->source.text, diagnostics);
  const auto &foreign_function = std::get<ast::Function>(foreign.crate->items[2]);
  assert(foreign_function.id == take.id);
  bool foreign_rejected = false;
  try {
    (void)bridge.function(foreign_function);
  } catch (const std::invalid_argument &) {
    foreign_rejected = true;
  }
  assert(foreign_rejected);
  assert(ir::print(ir::Module{declarations, {}}).find("@\"Packet::get\"") != std::string::npos);
  std::vector<decltype(ir::BinaryLowering::operation)> operations;
  std::vector<ir::ScalarArithmetic> assignments;
  const ast::Expr *first_binary = nullptr;
  ast::walk(*parsed.crate, [&](const auto &owner, const auto &payload) {
    using PayloadType = std::decay_t<decltype(payload)>;
    if constexpr (std::is_same_v<PayloadType, ast::BinaryExpr>) {
      auto lowered = bridge.binary(owner, payload);
      operations.push_back(lowered.operation);
      assert(lowered.left.node == payload.left->id);
      assert(lowered.right.node == payload.right->id);
      if (!first_binary) {
        first_binary = &owner;
      }
      if (payload.op == ast::BinaryOp::Lt || payload.op == ast::BinaryOp::LogicalAnd ||
          payload.op == ast::BinaryOp::LogicalOr) {
        assert(lowered.result_type == ir::Type::I1);
      } else {
        assert(lowered.result_type == ir::Type::I32);
      }
    }
    if constexpr (std::is_same_v<PayloadType, ast::CompoundAssignExpr>) {
      auto lowered = bridge.compound(owner, payload);
      assignments.push_back(lowered.operation);
      assert(lowered.target.node == payload.target->id);
      assert(lowered.value.node == payload.value->id);
      assert(index.expression(owner).type->kind() == rx::SemanticType::Kind::Unit);
    }
  });
  using Operation = decltype(ir::BinaryLowering::operation);
  const std::vector<Operation> expected{ir::ScalarArithmetic{ir::BinaryOp::SDiv, ir::Type::I32, {}},
                                        ir::ScalarArithmetic{ir::BinaryOp::UDiv, ir::Type::I32, {}},
                                        ir::ScalarArithmetic{ir::BinaryOp::AShr, ir::Type::I32, 31},
                                        ir::ScalarArithmetic{ir::BinaryOp::LShr, ir::Type::I32, 31},
                                        ir::ScalarComparison{ir::Predicate::SLt, ir::Type::I32},
                                        ir::ScalarComparison{ir::Predicate::ULt, ir::Type::I32},
                                        ir::ShortCircuit::And,
                                        ir::ShortCircuit::Or};
  assert(operations == expected);
  assert(assignments == (std::vector{ir::ScalarArithmetic{ir::BinaryOp::AShr, ir::Type::I32, 31},
                                     ir::ScalarArithmetic{ir::BinaryOp::UDiv, ir::Type::I32, {}}}));
  auto alien = rx::parse_source("fn main() { let x = 1 + 2; }", diagnostics);
  bool rejected = false;
  ast::walk(*alien.crate, [&](const auto &, const auto &payload) {
    if constexpr (std::is_same_v<std::decay_t<decltype(payload)>, ast::BinaryExpr>) {
      try {
        (void)bridge.binary(*first_binary, payload);
      } catch (const std::invalid_argument &) {
        rejected = true;
      }
    }
  });
  assert(rejected);
  rx::TypeContext types;
  auto integer = types.primitive(rx::SemanticType::Kind::I32);
  const auto packet_type = types.structure(index.declaration(packet.id).id, "Packet");
  assert(bridge.layout(types.array(packet_type, 2)).bytes == 24);
  assert(bridge.layout(types.box(packet_type)).bytes == target.pointer().bytes);
  assert(bridge.layout(types.vector(integer)).bytes == vector.storage.bytes);
  const auto unit = types.primitive(rx::SemanticType::Kind::Unit);
  assert(bridge.layout(types.array(unit, std::uint64_t{1} << 40)).bytes == 0);
  const auto largest_count = target.max_object_size() / target.integer().bytes;
  assert(bridge.layout(types.array(integer, largest_count)).bytes ==
         largest_count * target.integer().bytes);
  rejected = false;
  try {
    (void)bridge.layout(types.structure(rx::SymbolId{999999}, "Packet"));
  } catch (const std::out_of_range &) {
    rejected = true;
  }
  assert(rejected);
  auto array = types.array(integer, std::uint64_t{1} << 32);
  rejected = false;
  try {
    (void)bridge.layout(array);
  } catch (const std::overflow_error &) {
    rejected = true;
  }
  assert(rejected);
}
