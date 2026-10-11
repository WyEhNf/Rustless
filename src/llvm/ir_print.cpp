#include "rx/ir_verify.hpp"
#include "rx/ir_visit.hpp"

#include <array>
#include <sstream>
#include <stdexcept>
#include <string_view>

namespace rx::ir {
namespace {

std::string value(const Operand &operand) {
  return std::visit(ast::Overloaded{[](const LocalOperand &p) {
                                      return "%v" + std::to_string(p.id.value);
                                    },
                                    [](const GlobalOperand &p) {
                                      return "@" + symbol_name(p.symbol);
                                    },
                                    [&](const ConstantOperand &p) {
                                      return operand.type == Type::Ptr
                                                 ? std::string("null")
                                                 : std::to_string(p.bits);
                                    }},
                    operand.data);
}

std::string typed(const Operand &operand) {
  return type_name(operand.type) + " " + value(operand);
}

std::string label(BlockId id) { return "%b" + std::to_string(id.value); }

std::string_view predicate_name(Predicate predicate) {
  static constexpr std::array<std::string_view, 10> names = {
      "eq", "ne", "slt", "sle", "sgt", "sge", "ult", "ule", "ugt", "uge"};
  return names.at(static_cast<std::size_t>(predicate));
}

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

void print_instruction(std::ostringstream &out,
                       const Instruction &instruction) {
  out << "  ";
  if (auto result = definition(instruction))
    out << "%v" << result->id << " = ";
  visit(
      instruction,
      ast::Overloaded{
          [&](const Instruction &, const Alloca &p) {
            out << "alloca i8, i32 " << p.bytes << ", align " << p.alignment;
          },
          [&](const Instruction &, const Load &p) {
            out << "load " << type_name(p.result.type) << ", "
                << typed(p.address) << ", align " << p.alignment;
          },
          [&](const Instruction &, const Store &p) {
            out << "store " << typed(p.value) << ", " << typed(p.address)
                << ", align " << p.alignment;
          },
          [&](const Instruction &, const Gep &p) {
            out << "getelementptr i8, " << typed(p.base) << ", "
                << typed(p.offset);
          },
          [&](const Instruction &node, const Binary &p) {
            out << binary_opcode(opcode(node)) << ' ' << typed(p.left) << ", "
                << value(p.right);
          },
          [&](const Instruction &, const Compare &p) {
            out << "icmp " << predicate_name(p.predicate) << ' '
                << typed(p.left) << ", " << value(p.right);
          },
          [&](const Instruction &, const Cast &p) {
            constexpr std::array names{"zext", "trunc", "ptrtoint", "inttoptr"};
            out << names.at(static_cast<std::size_t>(p.op)) << ' '
                << typed(p.operand) << " to " << type_name(p.result.type);
          },
          [&](const Instruction &, const Call &p) {
            out << "call " << type_name(p.result ? p.result->type : Type::Void)
                << " @" << symbol_name(p.callee) << '(';
            for (std::size_t i = 0; i < p.arguments.size(); ++i) {
              if (i)
                out << ", ";
              out << typed(p.arguments[i]);
            }
            out << ')';
          },
          [&](const Instruction &, const Phi &p) {
            out << "phi " << type_name(p.result.type) << ' ';
            for (std::size_t i = 0; i < p.incoming.size(); ++i) {
              if (i)
                out << ", ";
              out << "[ " << value(p.incoming[i].value) << ", "
                  << label(p.incoming[i].predecessor) << " ]";
            }
          },
          [&](const Instruction &, const Branch &p) {
            out << "br label " << label(p.target);
          },
          [&](const Instruction &, const CondBranch &p) {
            out << "br " << typed(p.condition) << ", label "
                << label(p.consequent) << ", label " << label(p.alternative);
          },
          [&](const Instruction &, const Return &p) {
            out << "ret " << (p.value ? typed(*p.value) : "void");
          },
          [&](const Instruction &, const Unreachable &) {
            out << "unreachable";
          }});
  out << '\n';
}

} // namespace

std::string symbol_name(std::string_view name) {
  auto initial = [](unsigned char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' ||
           c == '.' || c == '$' || c == '-';
  };
  bool plain = !name.empty() && initial(name.front());
  for (unsigned char c : name)
    plain &= initial(c) || (c >= '0' && c <= '9');
  if (plain)
    return std::string(name);
  std::string result = "\"";
  constexpr std::string_view hex = "0123456789ABCDEF";
  for (unsigned char c : name) {
    if (c == '"' || c == '\\' || c < 32 || c >= 127) {
      result += '\\';
      result += hex[c >> 4];
      result += hex[c & 15];
    } else
      result += static_cast<char>(c);
  }
  return result + '"';
}

std::string print(const Module &module) {
  if (auto errors = verify(module); !errors.empty())
    throw std::invalid_argument("invalid IR in " + errors.front().function +
                                ": " + errors.front().message);
  std::ostringstream out;
  out << "; Rx LLVM IR: RV32IM / ILP32\n"
      << "target datalayout = \"e-m:e-p:32:32-i64:64-n32-S128\"\n"
      << "target triple = \"riscv32-unknown-elf\"\n\n";
  for (const auto &declaration : module.declarations) {
    out << "declare " << type_name(declaration.result) << " @"
        << symbol_name(declaration.name) << '(';
    for (std::size_t i = 0; i < declaration.parameters.size(); ++i) {
      if (i)
        out << ", ";
      out << type_name(declaration.parameters[i]);
    }
    out << ")\n";
  }
  for (const auto &function : module.functions) {
    out << "\ndefine " << type_name(function.result) << " @"
        << symbol_name(function.name) << '(';
    for (std::size_t i = 0; i < function.parameters.size(); ++i) {
      if (i)
        out << ", ";
      out << type_name(function.parameters[i].type) << " %v"
          << function.parameters[i].id;
    }
    out << ") {\n";
    for (const auto &block : function.blocks) {
      std::string comment = block.name;
      std::replace(comment.begin(), comment.end(), '\n', ' ');
      std::replace(comment.begin(), comment.end(), '\r', ' ');
      out << "b" << block.id << ": ; " << comment << '\n';
      for (const auto &instruction : block.instructions)
        print_instruction(out, instruction);
    }
    out << "}\n";
  }
  return out.str();
}

} // namespace rx::ir
