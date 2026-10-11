#include "rx/ir_builder.hpp"
#include "rx/ir_visit.hpp"

#undef NDEBUG
#include <cassert>
#include <stdexcept>

namespace ir = rx::ir;
namespace {
template <class F> void rejects(F &&f) {
  bool rejected = false;
  try { f(); } catch (const std::exception &) { rejected = true; }
  assert(rejected);
}
struct PartialVisitor {
  void operator()(const ir::Instruction &, const ir::Return &) {}
};
struct InconsistentVisitor {
  template <class P> int operator()(const ir::Instruction &, const P &) { return 0; }
  void operator()(const ir::Instruction &, const ir::Return &) {}
};
static_assert(!ir::ExhaustiveVisitor<PartialVisitor, ir::Instruction>);
static_assert(!ir::ExhaustiveVisitor<InconsistentVisitor, ir::Instruction>);
ir::Module diamond() {
  ir::Module module;
  module.functions.push_back({"Pair::choose", ir::Type::I32});
  auto &function = module.functions.back();
  ir::Builder builder(function);
  auto entry = builder.create_block("entry\ncomment");
  builder.select(entry);
  rx::ast::Node source;
  source.id = rx::ast::NodeId{7};
  source.span = {rx::ast::FileId{1}, 2, 9};
  builder.set_origin(source);
  auto address = builder.allocate(4);
  builder.store(ir::Operand::constant(ir::Type::I32, 8), address);
  auto value = builder.load(ir::Type::I32, address);
  // These pushes may reallocate the block vector while entry stays selected.
  auto left = builder.create_block("left"), right = builder.create_block("right");
  auto join = builder.create_block("join");
  builder.cond_branch(ir::Operand::constant(ir::Type::I1, 1), left, right);
  builder.select(left);
  auto sum = builder.binary(ir::BinaryOp::Add, value, ir::Operand::constant(ir::Type::I32, 1));
  builder.branch(join);
  builder.select(right);
  builder.branch(join);
  builder.select(join);
  auto merged = builder.phi(ir::Type::I32, {{sum, left}, {value, right}});
  builder.ret(merged);
  rejects([&] { builder.unreachable(); });
  assert(function.blocks[0].instructions[0].origin->node == source.id);
  return module;
}
} // namespace
int main() {
  auto module = diamond();
  assert(ir::verify(module).empty());
  auto llvm = ir::print(module);
  assert(llvm.find("@\"Pair::choose\"") != std::string::npos);
  assert(llvm.find("entry comment") != std::string::npos);
  assert(ir::symbol_name("a\"b\\c") == "\"a\\22b\\5Cc\"");
}
