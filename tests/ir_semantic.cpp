#include "rx/ast/visitor.hpp"
#include "rx/frontend.hpp"
#include "rx/ir_semantic.hpp"

#undef NDEBUG
#include <cassert>
#include <iostream>

namespace ast = rx::ast;
namespace ir = rx::ir;
int main() {
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
)", diagnostics);
  assert(parsed.syntax_ok && parsed.crate);
  auto checked = rx::analyze(*parsed.crate, diagnostics);
  diagnostics.print(std::cerr);
  assert(!diagnostics.has_errors());
  rx::SemanticIndex index(*parsed.crate, checked);
  ir::SemanticBridge bridge(index);
  const auto &packet = std::get<ast::Struct>(parsed.crate->items[0]);
  const auto &layout = bridge.structure(index.declaration(packet.id).id);
  assert(layout.layout.bytes == 12 && layout.layout.alignment == 4);
  assert(layout.fields.size() == 3);
  assert(layout.fields[0].offset == 0 && layout.fields[1].offset == 4 && layout.fields[2].offset == 8);
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
  assert(ir::print(ir::Module{declarations, {}}).find("@\"Packet::get\"") != std::string::npos);
}
