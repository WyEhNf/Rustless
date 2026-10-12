#include "rx/ir.hpp"

#include <array>
#include <stdexcept>
#include <utility>

namespace rx::ir {

Operand Operand::constant(Type type, std::uint32_t bits) {
  if (!is_value_type(type) || (type == Type::I1 && bits > 1) || (type == Type::I8 && bits > 255) ||
      (type == Type::Ptr && bits != 0)) {
    throw std::invalid_argument("constant does not fit its IR type");
  }
  return {type, ConstantOperand{bits}};
}

Operand Operand::local(Type type, ValueId id) {
  if (!id.value || !is_value_type(type)) {
    throw std::invalid_argument("local operand requires a value identity and type");
  }
  return {type, LocalOperand{id}};
}

Operand Operand::global(std::string name) {
  if (name.empty()) {
    throw std::invalid_argument("global operand requires a name");
  }
  return {Type::Ptr, GlobalOperand{std::move(name)}};
}

bool is_terminator(Op op) {
  return op == Op::Br || op == Op::CondBr || op == Op::Ret || op == Op::Unreachable;
}

bool has_side_effects(const Instruction &instruction) {
  auto op = opcode(instruction);
  return op == Op::Store || op == Op::Call || is_terminator(op);
}

Op opcode(const Instruction &instruction) {
  return std::visit(ast::Overloaded{
                        [](const Alloca &) { return Op::Alloca; },
                        [](const Load &) { return Op::Load; },
                        [](const Store &) { return Op::Store; },
                        [](const Gep &) { return Op::Gep; },
                        [](const Binary &payload) {
                          constexpr std::array ops{Op::Add,
                                                   Op::Sub,
                                                   Op::Mul,
                                                   Op::SDiv,
                                                   Op::UDiv,
                                                   Op::SRem,
                                                   Op::URem,
                                                   Op::And,
                                                   Op::Or,
                                                   Op::Xor,
                                                   Op::Shl,
                                                   Op::LShr,
                                                   Op::AShr};
                          return ops.at(static_cast<std::size_t>(payload.op));
                        },
                        [](const Compare &) { return Op::ICmp; },
                        [](const Cast &payload) {
                          constexpr std::array ops{Op::ZExt, Op::Trunc, Op::PtrToInt, Op::IntToPtr};
                          return ops.at(static_cast<std::size_t>(payload.op));
                        },
                        [](const Call &) { return Op::Call; },
                        [](const Phi &) { return Op::Phi; },
                        [](const Branch &) { return Op::Br; },
                        [](const CondBranch &) { return Op::CondBr; },
                        [](const Return &) { return Op::Ret; },
                        [](const Unreachable &) { return Op::Unreachable; },
                    },
                    instruction.data);
}
bool is_terminator(const Instruction &instruction) {
  return is_terminator(opcode(instruction));
}

std::optional<Result> definition(const Instruction &instruction) {
  using Maybe = std::optional<Result>;
  return std::visit(
      ast::Overloaded{
          [](const Alloca &payload) -> Maybe { return Result{payload.result, Type::Ptr}; },
          [](const Load &payload) -> Maybe { return payload.result; },
          [](const Store &) -> Maybe { return {}; },
          [](const Gep &payload) -> Maybe { return Result{payload.result, Type::Ptr}; },
          [](const Binary &payload) -> Maybe { return payload.result; },
          [](const Compare &payload) -> Maybe { return Result{payload.result, Type::I1}; },
          [](const Cast &payload) -> Maybe { return payload.result; },
          [](const Call &payload) -> Maybe { return payload.result; },
          [](const Phi &payload) -> Maybe { return payload.result; },
          [](const Branch &) -> Maybe { return {}; },
          [](const CondBranch &) -> Maybe { return {}; },
          [](const Return &) -> Maybe { return {}; },
          [](const Unreachable &) -> Maybe { return {}; },
      },
      instruction.data);
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
  throw std::logic_error("unknown IR type");
}

bool is_value_type(Type type) {
  return type == Type::I1 || type == Type::I8 || type == Type::I32 || type == Type::Ptr;
}
unsigned integer_width(Type type) {
  switch (type) {
  case Type::I1:
    return 1;
  case Type::I8:
    return 8;
  case Type::I32:
    return 32;
  default:
    return 0;
  }
}

}
