#include "rx/llvm/ir/visit.hpp"

#undef NDEBUG
#include <cassert>
#include <fstream>
#include <stdexcept>
#include <type_traits>

namespace ir = rx::ir;
static_assert(!std::is_same_v<ir::ValueId, ir::BlockId>);
static_assert(!std::is_same_v<ir::ValueId, rx::ast::NodeId>);
static_assert(!std::is_default_constructible_v<ir::Instruction>);
static_assert(!std::is_default_constructible_v<ir::Operand>);

int main(int argc, char **argv) {
  using namespace ir;
  auto constant = [](std::uint32_t bits) { return Operand::constant(Type::I32, bits); };
  auto local = [](Type type, std::uint32_t id) { return Operand::local(type, ValueId{id}); };
  const auto one = constant(1);
  const auto condition = local(Type::I1, 1);
  const auto external = Operand::global("printInt");
  assert(std::get<ConstantOperand>(one.data).bits == 1);
  assert(std::get<LocalOperand>(condition.data).id == ValueId{1});
  assert(std::get<GlobalOperand>(external.data).symbol == "printInt");
  assert(type_name(Type::I32) == "i32" && type_name(Type::Ptr) == "ptr");
  assert(is_terminator(Op::CondBr) && !is_terminator(Op::Load));
  assert(has_side_effects(Instruction{Store{one, local(Type::Ptr, 2)}}));
  assert(!has_side_effects(Instruction{Load{{ValueId{3}, Type::I32}, local(Type::Ptr, 2)}}));

  Module module;
  module.declarations.push_back({"printInt", Type::Void, {Type::I32}});
  Function function;
  function.name = "choose";
  function.result = Type::I32;
  function.parameters.push_back({ValueId{1}, Type::I1, "condition"});
  function.blocks = {
      {BlockId{0}, "entry", {Instruction{CondBranch{condition, BlockId{1}, BlockId{2}}}}},
      {BlockId{1}, "yes", {Instruction{Return{one}}}},
      {BlockId{2}, "no", {Instruction{Return{constant(0)}}}}};
  module.functions.push_back(function);
  const auto llvm = print(module);
  assert(llvm.find("declare void @printInt(i32)") != std::string::npos);
  assert(llvm.find("define i32 @choose(i1 %v1)") != std::string::npos);
  assert(llvm.find("br i1 %v1, label %b1, label %b2") != std::string::npos);
  assert(llvm.find("ret i32 1") != std::string::npos);

  Function sample;
  sample.name = "sample";
  sample.result = Type::I32;
  sample.blocks.push_back(
      {BlockId{0},
       "entry",
       {Instruction{Alloca{ValueId{1}, 4}},
        Instruction{Store{constant(7), local(Type::Ptr, 1)}},
        Instruction{Gep{ValueId{2}, local(Type::Ptr, 1), constant(0)}},
        Instruction{Load{{ValueId{3}, Type::I32}, local(Type::Ptr, 2)}},
        Instruction{
            Binary{{ValueId{4}, Type::I32}, BinaryOp::Add, local(Type::I32, 3), constant(1)}},
        Instruction{Compare{ValueId{5}, Predicate::SGt, local(Type::I32, 4), constant(0)}},
        Instruction{Cast{{ValueId{6}, Type::I32}, CastOp::ZExt, local(Type::I1, 5)}},
        Instruction{Call{{}, "printInt", {local(Type::I32, 6)}}},
        Instruction{Return{local(Type::I32, 6)}}}});
  module.functions.push_back(sample);
  Function join;
  join.name = "join";
  join.result = Type::I32;
  join.blocks = {{BlockId{0}, "entry", {Instruction{Branch{BlockId{1}}}}},
                 {BlockId{1},
                  "merge",
                  {Instruction{Phi{{ValueId{1}, Type::I32}, {{constant(9), BlockId{0}}}}},
                   Instruction{Return{local(Type::I32, 1)}}}}};
  module.functions.push_back(join);
  const auto expanded = print(module);
  if (argc == 2) {
    std::ofstream output(argv[1]);
    output << expanded;
    assert(output.good());
  }
  for (const auto *expected : {"alloca i8, i32 4, align 4",
                               "store i32 7, ptr %v1",
                               "getelementptr i8, ptr %v1, i32 0",
                               "%v4 = add i32 %v3, 1",
                               "icmp sgt i32 %v4, 0",
                               "zext i1 %v5 to i32",
                               "call void @printInt(i32 %v6)",
                               "phi i32 [ 9, %b0 ]"}) {
    assert(expanded.find(expected) != std::string::npos);
  }
  bool rejected = false;
  try {
    (void)Operand::local(Type::I32, ValueId{});
  } catch (const std::invalid_argument &) {
    rejected = true;
  }
  assert(rejected);
  module.functions[1].blocks[0].instructions[4].data =
      Binary{{ValueId{4}, Type::I32}, static_cast<BinaryOp>(255), local(Type::I32, 3), constant(1)};
  rejected = false;
  try {
    (void)print(module);
  } catch (const std::exception &) {
    rejected = true;
  }
  assert(rejected);
}
