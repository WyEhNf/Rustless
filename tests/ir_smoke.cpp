#include "rx/ir.hpp"

#undef NDEBUG
#include <cassert>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

int main() {
  using namespace rx::ir;
  const auto one = Operand::constant(Type::I32, 1);
  const auto condition = Operand::local(Type::I1, ValueId{1});
  const auto external = Operand::global("printInt");
  assert(one.kind == Operand::Kind::Constant && one.value == 1);
  assert(condition.kind == Operand::Kind::Local && condition.value == 1);
  assert(external.kind == Operand::Kind::Global && external.type == Type::Ptr);
  assert(type_name(Type::I32) == "i32" && type_name(Type::Ptr) == "ptr");
  assert(is_terminator(Op::CondBr) && !is_terminator(Op::Load));
  assert(has_side_effects(Instruction{.op = Op::Store}));
  assert(!has_side_effects(Instruction{.op = Op::Load}));

  Module module;
  module.declarations.push_back({"printInt", Type::Void, {Type::I32}});
  Function function;
  function.name = "choose";
  function.result = Type::I32;
  function.parameters.push_back({ValueId{1}, Type::I1, "condition"});
  Block entry{BlockId{0}, "entry", {}};
  Instruction branch;
  branch.op = Op::CondBr;
  branch.operands = {condition};
  branch.targets = {BlockId{1}, BlockId{2}};
  entry.instructions.push_back(branch);
  Block yes{BlockId{1}, "yes", {}};
  Instruction ret_one;
  ret_one.op = Op::Ret;
  ret_one.operands = {one};
  yes.instructions.push_back(ret_one);
  Block no{BlockId{2}, "no", {}};
  Instruction ret_zero;
  ret_zero.op = Op::Ret;
  ret_zero.operands = {Operand::constant(Type::I32, 0)};
  no.instructions.push_back(ret_zero);
  function.blocks = {entry, yes, no};
  module.functions.push_back(function);

  const std::string llvm = print(module);
  assert(llvm.find("declare void @printInt(i32)") != std::string::npos);
  assert(llvm.find("define i32 @choose(i1 %v1)") != std::string::npos);
  assert(llvm.find("br i1 %v1, label %b1, label %b2") != std::string::npos);
  assert(llvm.find("ret i32 1") != std::string::npos);

  auto make = [](Op op, ValueId id, Type type,
                 std::vector<Operand> operands = {}) {
    Instruction instruction;
    instruction.op = op;
    instruction.result = id;
    instruction.type = type;
    instruction.operands = std::move(operands);
    return instruction;
  };
  Function sample;
  sample.name = "sample";
  sample.result = Type::I32;
  Block body{BlockId{0}, "entry", {}};
  auto slot = make(Op::Alloca, ValueId{1}, Type::Ptr);
  slot.bytes = 4;
  body.instructions.push_back(slot);
  body.instructions.push_back(make(Op::Store, ValueId{0}, Type::Void,
      {Operand::constant(Type::I32, 7), Operand::local(Type::Ptr, ValueId{1})}));
  body.instructions.push_back(make(Op::Gep, ValueId{2}, Type::Ptr,
      {Operand::local(Type::Ptr, ValueId{1}), Operand::constant(Type::I32, 0)}));
  body.instructions.push_back(make(Op::Load, ValueId{3}, Type::I32,
      {Operand::local(Type::Ptr, ValueId{2})}));
  body.instructions.push_back(make(Op::Add, ValueId{4}, Type::I32,
      {Operand::local(Type::I32, ValueId{3}), Operand::constant(Type::I32, 1)}));
  auto compare = make(Op::ICmp, ValueId{5}, Type::I1,
      {Operand::local(Type::I32, ValueId{4}), Operand::constant(Type::I32, 0)});
  compare.predicate = Predicate::SGt;
  body.instructions.push_back(compare);
  body.instructions.push_back(make(Op::ZExt, ValueId{6}, Type::I32,
      {Operand::local(Type::I1, ValueId{5})}));
  auto call = make(Op::Call, ValueId{0}, Type::Void,
      {Operand::local(Type::I32, ValueId{6})});
  call.callee = "printInt";
  body.instructions.push_back(call);
  body.instructions.push_back(make(Op::Ret, ValueId{0}, Type::Void,
      {Operand::local(Type::I32, ValueId{6})}));
  sample.blocks.push_back(body);
  module.functions.push_back(sample);

  Function join;
  join.name = "join";
  join.result = Type::I32;
  join.blocks = {{BlockId{0}, "entry", {make(Op::Br, ValueId{0}, Type::Void)}},
                 {BlockId{1}, "merge", {}}};
  join.blocks[0].instructions[0].targets = {BlockId{1}};
  auto phi = make(Op::Phi, ValueId{1}, Type::I32,
      {Operand::constant(Type::I32, 9)});
  phi.targets = {BlockId{0}};
  join.blocks[1].instructions.push_back(phi);
  join.blocks[1].instructions.push_back(make(Op::Ret, ValueId{0}, Type::Void,
      {Operand::local(Type::I32, ValueId{1})}));
  module.functions.push_back(join);
  const std::string expanded = print(module);
  assert(expanded.find("alloca i8, i32 4, align 4") != std::string::npos);
  assert(expanded.find("store i32 7, ptr %v1") != std::string::npos);
  assert(expanded.find("getelementptr i8, ptr %v1, i32 0") != std::string::npos);
  assert(expanded.find("%v4 = add i32 %v3, 1") != std::string::npos);
  assert(expanded.find("icmp sgt i32 %v4, 0") != std::string::npos);
  assert(expanded.find("zext i1 %v5 to i32") != std::string::npos);
  assert(expanded.find("call void @printInt(i32 %v6)") != std::string::npos);
  assert(expanded.find("phi i32 [ 9, %b0 ]") != std::string::npos);

  module.functions[0].blocks[0].instructions[0].op = static_cast<Op>(255);
  bool rejected = false;
  try {
    (void)print(module);
  } catch (const std::logic_error &) {
    rejected = true;
  }
  assert(rejected);
}
