#include "rx/llvm/ir/verify.hpp"
#include "rx/llvm/ir/visit.hpp"
#include "rx/llvm/ir/target.hpp"

#include <algorithm>
#include <array>
#include <sstream>
#include <stdexcept>
#include <string_view>

namespace rx::ir {
namespace {

std::string value(const Operand &operand) {
  return std::visit(
      ast::Overloaded{
          [](const LocalOperand &payload) { return "%v" + std::to_string(payload.id.value); },
          [](const GlobalOperand &payload) { return "@" + symbol_name(payload.symbol); },
          [&](const ConstantOperand &payload) {
            if (operand.type == Type::Ptr) {
              return std::string("null");
            }
            return std::to_string(payload.bits);
          },
      },
      operand.data);
}

std::string typed(const Operand &operand) {
  return type_name(operand.type) + " " + value(operand);
}

std::string label(BlockId id) {
  return "%b" + std::to_string(id.value);
}

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

void print_instruction(std::ostringstream &out, const Instruction &instruction) {
  out << "  ";
  if (auto result = definition(instruction)) {
    out << "%v" << result->id << " = ";
  }
  auto print_payload = ast::Overloaded{
      [&](const Instruction &, const Alloca &payload) {
        out << "alloca i8, i32 " << payload.bytes << ", align " << payload.alignment;
      },
      [&](const Instruction &, const Load &payload) {
        out << "load " << type_name(payload.result.type) << ", " << typed(payload.address)
            << ", align " << payload.alignment;
      },
      [&](const Instruction &, const Store &payload) {
        out << "store " << typed(payload.value) << ", " << typed(payload.address) << ", align "
            << payload.alignment;
      },
      [&](const Instruction &, const Gep &payload) {
        out << "getelementptr i8, " << typed(payload.base) << ", " << typed(payload.offset);
      },
      [&](const Instruction &node, const Binary &payload) {
        out << binary_opcode(opcode(node)) << ' ' << typed(payload.left) << ", "
            << value(payload.right);
      },
      [&](const Instruction &, const Compare &payload) {
        out << "icmp " << predicate_name(payload.predicate) << ' ' << typed(payload.left) << ", "
            << value(payload.right);
      },
      [&](const Instruction &, const Cast &payload) {
        constexpr std::array names{"zext", "trunc", "ptrtoint", "inttoptr"};
        out << names.at(static_cast<std::size_t>(payload.op)) << ' ' << typed(payload.operand)
            << " to " << type_name(payload.result.type);
      },
      [&](const Instruction &, const Call &payload) {
        Type result_type = Type::Void;
        if (payload.result) {
          result_type = payload.result->type;
        }
        out << "call " << type_name(result_type) << " @" << symbol_name(payload.callee) << '(';
        for (std::size_t index = 0; index < payload.arguments.size(); ++index) {
          if (index) {
            out << ", ";
          }
          out << typed(payload.arguments[index]);
        }
        out << ')';
      },
      [&](const Instruction &, const Phi &payload) {
        out << "phi " << type_name(payload.result.type) << ' ';
        for (std::size_t index = 0; index < payload.incoming.size(); ++index) {
          if (index) {
            out << ", ";
          }
          out << "[ " << value(payload.incoming[index].value) << ", "
              << label(payload.incoming[index].predecessor) << " ]";
        }
      },
      [&](const Instruction &, const Branch &payload) {
        out << "br label " << label(payload.target);
      },
      [&](const Instruction &, const CondBranch &payload) {
        out << "br " << typed(payload.condition) << ", label " << label(payload.consequent)
            << ", label " << label(payload.alternative);
      },
      [&](const Instruction &, const Return &payload) {
        out << "ret ";
        if (payload.value) {
          out << typed(*payload.value);
        } else {
          out << "void";
        }
      },
      [&](const Instruction &, const Unreachable &) { out << "unreachable"; },
  };
  visit(instruction, print_payload);
  out << '\n';
}

}

std::string symbol_name(std::string_view name) {
  auto initial = [](unsigned char character) {
    return (character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z') ||
           character == '_' || character == '.' || character == '$' || character == '-';
  };
  bool plain = !name.empty() && initial(name.front());
  for (unsigned char character : name) {
    plain &= initial(character) || (character >= '0' && character <= '9');
  }
  if (plain) {
    return std::string(name);
  }
  std::string result = "\"";
  constexpr std::string_view hex = "0123456789ABCDEF";
  for (unsigned char character : name) {
    if (character == '"' || character == '\\' || character < 32 || character >= 127) {
      result += '\\';
      result += hex[character >> 4];
      result += hex[character & 15];
    } else {
      result += static_cast<char>(character);
    }
  }
  return result + '"';
}

std::string print(const Module &module) {
  if (auto errors = verify(module); !errors.empty()) {
    throw std::invalid_argument("invalid IR in " + errors.front().function + ": " +
                                errors.front().message);
  }
  std::ostringstream out;
  out << "; Rx LLVM IR: RV32IM / ILP32\n"
      << "target datalayout = \"" << TargetLayout::rv32().llvm_data_layout() << "\"\n"
      << "target triple = \"" << TargetLayout::rv32().triple() << "\"\n\n";
  for (const auto &declaration : module.declarations) {
    out << "declare " << type_name(declaration.result) << " @" << symbol_name(declaration.name)
        << '(';
    for (std::size_t index = 0; index < declaration.parameters.size(); ++index) {
      if (index) {
        out << ", ";
      }
      out << type_name(declaration.parameters[index]);
    }
    out << ")\n";
  }
  for (const auto &function : module.functions) {
    out << "\ndefine " << type_name(function.result) << " @" << symbol_name(function.name) << '(';
    for (std::size_t index = 0; index < function.parameters.size(); ++index) {
      if (index) {
        out << ", ";
      }
      out << type_name(function.parameters[index].type) << " %v" << function.parameters[index].id;
    }
    out << ") {\n";
    for (const auto &block : function.blocks) {
      std::string comment = block.name;
      std::replace(comment.begin(), comment.end(), '\n', ' ');
      std::replace(comment.begin(), comment.end(), '\r', ' ');
      out << "b" << block.id << ": ; " << comment << '\n';
      for (const auto &instruction : block.instructions) {
        print_instruction(out, instruction);
      }
    }
    out << "}\n";
  }
  return out.str();
}

}
