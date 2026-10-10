#include "rx/ir.hpp"

#include <sstream>
#include <stdexcept>
#include <string_view>

namespace rx::ir {
namespace {

std::string value(const Operand &operand) {
  if (operand.kind == Operand::Kind::Local)
    return "%v" + std::to_string(operand.value);
  if (operand.kind == Operand::Kind::Global)
    return "@" + operand.symbol;
  if (operand.type == Type::Ptr)
    return "null";
  return std::to_string(operand.value);
}

std::string typed(const Operand &operand) {
  return type_name(operand.type) + " " + value(operand);
}

std::string label(BlockId id) { return "%b" + std::to_string(id); }

std::string_view binary_opcode(Op op) {
  switch (op) {
  case Op::Add:
    return "add";
  case Op::Sub:
    return "sub";
  case Op::Mul:
    return "mul";
  case Op::SDiv:
    return "sdiv";
  case Op::UDiv:
    return "udiv";
  case Op::SRem:
    return "srem";
  case Op::URem:
    return "urem";
  case Op::And:
    return "and";
  case Op::Or:
    return "or";
  case Op::Xor:
    return "xor";
  case Op::Shl:
    return "shl";
  case Op::LShr:
    return "lshr";
  case Op::AShr:
    return "ashr";
  default:
    throw std::logic_error("not a binary opcode");
  }
}

void print_instruction(std::ostringstream &out, const Instruction &instruction) {
  out << "  ";
  if (instruction.result)
    out << "%v" << instruction.result << " = ";
  switch (instruction.op) {
  case Op::Add:
  case Op::Sub:
  case Op::Mul:
  case Op::SDiv:
  case Op::UDiv:
  case Op::SRem:
  case Op::URem:
  case Op::And:
  case Op::Or:
  case Op::Xor:
  case Op::Shl:
  case Op::LShr:
  case Op::AShr:
    out << binary_opcode(instruction.op) << ' '
        << typed(instruction.operands.at(0)) << ", "
        << value(instruction.operands.at(1));
    break;
  case Op::Br:
    out << "br label " << label(instruction.targets.at(0));
    break;
  case Op::CondBr:
    out << "br " << typed(instruction.operands.at(0)) << ", label "
        << label(instruction.targets.at(0)) << ", label "
        << label(instruction.targets.at(1));
    break;
  case Op::Ret:
    out << "ret ";
    if (instruction.operands.empty())
      out << "void";
    else
      out << typed(instruction.operands.at(0));
    break;
  case Op::Unreachable:
    out << "unreachable";
    break;
  default:
    throw std::logic_error("IR printer does not support this opcode yet");
  }
  out << '\n';
}

} // namespace

std::string print(const Module &module) {
  std::ostringstream out;
  out << "; Rx LLVM IR: RV32IM / ILP32\n"
      << "target datalayout = \"e-m:e-p:32:32-i64:64-n32-S128\"\n"
      << "target triple = \"riscv32-unknown-elf\"\n\n";
  for (const auto &declaration : module.declarations) {
    out << "declare " << type_name(declaration.result) << " @"
        << declaration.name << '(';
    for (std::size_t i = 0; i < declaration.parameters.size(); ++i) {
      if (i)
        out << ", ";
      out << type_name(declaration.parameters[i]);
    }
    out << ")\n";
  }
  for (const auto &function : module.functions) {
    out << "\ndefine " << type_name(function.result) << " @"
        << function.name << '(';
    for (std::size_t i = 0; i < function.parameters.size(); ++i) {
      if (i)
        out << ", ";
      out << type_name(function.parameters[i].type) << " %v"
          << function.parameters[i].id;
    }
    out << ") {\n";
    for (const auto &block : function.blocks) {
      out << "b" << block.id << ": ; " << block.name << '\n';
      for (const auto &instruction : block.instructions)
        print_instruction(out, instruction);
    }
    out << "}\n";
  }
  return out.str();
}

} // namespace rx::ir
