#include "rx/ir.hpp"

#include <utility>
#include <stdexcept>

namespace rx::ir {

Operand Operand::constant(Type type, std::uint32_t bits) {
  if (type == Type::Void || (type == Type::I1 && bits > 1) ||
      (type == Type::I8 && bits > 255) || (type == Type::Ptr && bits != 0))
    throw std::invalid_argument("constant does not fit its IR type");
  return {type, ConstantOperand{bits}};
}

Operand Operand::local(Type type, ValueId id) {
  if (!id.value || type == Type::Void)
    throw std::invalid_argument("local operand requires a value identity and type");
  return {type, LocalOperand{id}};
}

Operand Operand::global(std::string name) {
  if (name.empty())
    throw std::invalid_argument("global operand requires a name");
  return {Type::Ptr, GlobalOperand{std::move(name)}};
}

bool is_terminator(Op op) {
  return op == Op::Br || op == Op::CondBr || op == Op::Ret ||
         op == Op::Unreachable;
}

bool has_side_effects(const Instruction &instruction) {
  return instruction.op == Op::Store || instruction.op == Op::Call ||
         is_terminator(instruction.op);
}

std::string type_name(Type type) {
  switch (type) {
  case Type::Void:
    return "void";
  case Type::I1:
    return "i1";
  case Type::I8:
    return "i8";
  case Type::I32:
    return "i32";
  case Type::Ptr:
    return "ptr";
  }
  return "void";
}

} // namespace rx::ir
