#include "rx/ir.hpp"

#include <utility>

namespace rx::ir {

Operand Operand::constant(Type type, std::uint32_t bits) {
  return {type, Kind::Constant, bits, {}};
}

Operand Operand::local(Type type, ValueId id) {
  return {type, Kind::Local, id.value, {}};
}

Operand Operand::global(std::string name) {
  return {Type::Ptr, Kind::Global, 0, std::move(name)};
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
