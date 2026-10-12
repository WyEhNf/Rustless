#include "rx/ir_builder.hpp"
#include "rx/ir_visit.hpp"

#undef NDEBUG
#include <cassert>
#include <fstream>
#include <stdexcept>

namespace ir = rx::ir;
namespace {
template <class F> void rejects(F &&f) {
  bool rejected = false;
  try {
    f();
  } catch (const std::exception &) {
    rejected = true;
  }
  assert(rejected);
}
struct PartialVisitor {
  void operator()(const ir::Instruction &, const ir::Return &) {}
};
struct InconsistentVisitor {
  template <class P> int operator()(const ir::Instruction &, const P &) {
    return 0;
  }
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
  source.span = {.begin = 2, .end = 9, .file = rx::FileId{1}};
  builder.set_origin(source);
  auto address = builder.allocate(4);
  builder.store(ir::Operand::constant(ir::Type::I32, 8), address);
  auto value = builder.load(ir::Type::I32, address);
  // These pushes may reallocate the block vector while entry stays selected.
  auto left = builder.create_block("left"),
       right = builder.create_block("right");
  auto join = builder.create_block("join");
  builder.cond_branch(ir::Operand::constant(ir::Type::I1, 1), left, right);
  builder.select(left);
  auto sum = builder.binary(ir::BinaryOp::Add, value,
                            ir::Operand::constant(ir::Type::I32, 1));
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
ir::Module parallel_edges() {
  ir::Function function{"parallel_edges", ir::Type::I32};
  ir::Builder builder(function);
  auto entry = builder.create_block("entry");
  auto join = builder.create_block("join");
  builder.select(entry);
  auto value = builder.binary(ir::BinaryOp::Add,
                              ir::Operand::constant(ir::Type::I32, 3),
                              ir::Operand::constant(ir::Type::I32, 4));
  builder.cond_branch(ir::Operand::constant(ir::Type::I1, 1), join, join);
  builder.select(join);
  auto merged = builder.phi(ir::Type::I32, {{value, entry}, {value, entry}});
  builder.ret(merged);
  return {{}, {std::move(function)}};
}
void check_parallel_edges() {
  auto module = parallel_edges();
  assert(ir::verify(module).empty());
  assert(ir::print(module).find("phi i32") != std::string::npos);
  auto &phi = std::get<ir::Phi>(module.functions[0].blocks[1].instructions[0].data);
  auto second = phi.incoming.back();
  phi.incoming.pop_back();
  assert(!ir::verify(module).empty()); // One incoming cannot cover two edges.
  phi.incoming.push_back(second);
  phi.incoming.back().value = ir::Operand::constant(ir::Type::I32, 8);
  assert(!ir::verify(module).empty()); // Duplicate edges cannot select two values.
  phi.incoming.back() = second;
  phi.incoming.push_back(second);
  assert(!ir::verify(module).empty()); // Three incoming values exceed two edges.
  phi.incoming.pop_back();
  auto &branch =
      std::get<ir::CondBranch>(module.functions[0].blocks[0].instructions.back().data);
  module.functions[0].blocks[0].instructions.back().data =
      ir::Branch{branch.consequent};
  assert(!ir::verify(module).empty()); // Two incoming values cannot cover one edge.
  phi.incoming.pop_back();
  assert(ir::verify(module).empty());
}
} // namespace
int main(int argc, char **argv) {
  check_parallel_edges();
  auto module = diamond();
  assert(ir::verify(module).empty());
  auto llvm = ir::print(module);
  if (argc == 2) {
    std::ofstream output(argv[1]);
    output << llvm;
    assert(output.good());
  }
  assert(llvm.find("@\"Pair::choose\"") != std::string::npos);
  assert(llvm.find("entry comment") != std::string::npos);
  assert(ir::symbol_name("a\"b\\c") == "\"a\\22b\\5Cc\"");
  auto invalid = [&](auto mutate) {
    auto broken = module;
    mutate(broken.functions[0]);
    auto errors = ir::verify(broken);
    assert(!errors.empty());
    rejects([&] { (void)ir::print(broken); });
    return errors;
  };
  auto errors = invalid([](ir::Function &f) {
    auto &binary = std::get<ir::Binary>(f.blocks[1].instructions[0].data);
    binary.left = ir::Operand::local(ir::Type::I32, ir::ValueId{999});
  });
  assert(errors[0].origin && errors[0].origin->node.value == 7);
  invalid([](ir::Function &f) {
    auto &ret = std::get<ir::Return>(f.blocks[3].instructions.back().data);
    ret.value = ir::Operand::local(
        ir::Type::I32, ir::definition(f.blocks[1].instructions[0])->id);
  }); // A left-only value cannot dominate the join.
  invalid([](ir::Function &f) {
    auto &phi = std::get<ir::Phi>(f.blocks[3].instructions[0].data);
    phi.incoming.pop_back();
  });
  invalid([](ir::Function &f) {
    auto &load = std::get<ir::Load>(f.blocks[0].instructions[2].data);
    load.address = ir::Operand::local(
        ir::Type::Ptr, ir::definition(f.blocks[1].instructions[0])->id);
  });
  invalid([](ir::Function &f) {
    auto &branch = std::get<ir::Branch>(f.blocks[2].instructions[0].data);
    branch.target = ir::BlockId{999};
  });
  invalid([](ir::Function &f) {
    f.blocks[3].instructions.emplace_back(ir::Unreachable{});
  });
  auto integer = ir::Operand::constant(ir::Type::I32, 1);
  assert(!ir::verify_instruction(
              ir::Instruction{ir::Cast{
                  {ir::ValueId{1}, ir::Type::I8}, ir::CastOp::ZExt, integer}})
              .empty());
  assert(
      !ir::verify_instruction(ir::Instruction{ir::Load{
                                  {ir::ValueId{1}, ir::Type::I32}, integer, 3}})
           .empty());
  ir::Function function{"calls", ir::Type::Void};
  ir::Builder builder(function);
  builder.select(builder.create_block("entry"));
  auto before = function.blocks[0].instructions.size();
  rejects([&] {
    builder.binary(ir::BinaryOp::Add, integer,
                   ir::Operand::constant(ir::Type::I1, 0));
  });
  assert(function.blocks[0].instructions.size() == before);
  assert(!builder.call(ir::Type::Void, "external", {}));
  builder.ret();
  ir::Module calls{{}, {function}};
  assert(!ir::verify(calls).empty());
  calls.declarations.push_back({"external", ir::Type::Void, {}});
  assert(ir::verify(calls).empty());
  calls.functions[0].symbol = rx::SymbolId{};
  assert(!ir::verify(calls).empty());
  calls.functions[0].symbol.reset();
  calls.functions[0].blocks[0].instructions.insert(
      calls.functions[0].blocks[0].instructions.begin(),
      ir::Instruction{ir::Load{{ir::ValueId{99}, ir::Type::I32},
                               ir::Operand::global("missing")}});
  assert(!ir::verify(calls).empty());
}
