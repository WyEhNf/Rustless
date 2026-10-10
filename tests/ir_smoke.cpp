#include "rx/ir.hpp"

#undef NDEBUG
#include <cassert>
#include <stdexcept>
#include <string>

int main() {
  using namespace rx::ir;
  const auto one = Operand::constant(Type::I32, 1);
  const auto condition = Operand::local(Type::I1, 1);
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
  function.parameters.push_back({1, Type::I1, "condition"});
  Block entry{0, "entry", {}};
  Instruction branch;
  branch.op = Op::CondBr;
  branch.operands = {condition};
  branch.targets = {1, 2};
  entry.instructions.push_back(branch);
  Block yes{1, "yes", {}};
  Instruction ret_one;
  ret_one.op = Op::Ret;
  ret_one.operands = {one};
  yes.instructions.push_back(ret_one);
  Block no{2, "no", {}};
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

  module.functions[0].blocks[0].instructions[0].op = static_cast<Op>(255);
  bool rejected = false;
  try {
    (void)print(module);
  } catch (const std::logic_error &) {
    rejected = true;
  }
  assert(rejected);
}
