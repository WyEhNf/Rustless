#include "rx/ir_verify.hpp"
#include <map>
#include <set>

namespace rx::ir {
std::vector<std::string> verify_instruction(const Instruction &node) {
  std::vector<std::string> errors;
  auto require = [&](bool ok, const char *message) { if (!ok) errors.emplace_back(message); };
  if (node.data.valueless_by_exception()) return {"instruction has no payload"};
  try { (void)opcode(node); }
  catch (const std::exception &) { errors.emplace_back("invalid instruction operator"); }
  if (auto result = definition(node))
    require(result->id.value && is_value_type(result->type), "invalid SSA result");
  if (node.origin)
    require(node.origin->node.value && node.origin->span.file.value &&
            node.origin->span.begin <= node.origin->span.end, "invalid syntax provenance");
  for_each_operand(node, [&](const Operand &operand) {
    require(is_value_type(operand.type), "operand has an invalid value type");
    visit(operand, ast::Overloaded{
      [&](const Operand &owner, const ConstantOperand &p) {
        require((owner.type != Type::I1 || p.bits <= 1) &&
                (owner.type != Type::I8 || p.bits <= 255) &&
                (owner.type != Type::Ptr || p.bits == 0), "constant does not fit its type");
      },
      [&](const Operand &, const LocalOperand &p) { require(p.id.value != 0, "zero SSA operand"); },
      [&](const Operand &owner, const GlobalOperand &p) {
        require(owner.type == Type::Ptr && !p.symbol.empty(), "invalid global operand");
      }});
  });
  auto alignment = [&](std::uint32_t value) {
    require(value && !(value & (value - 1)), "alignment must be a nonzero power of two");
  };
  visit(node, ast::Overloaded{
    [&](const Instruction &, const Alloca &p) { alignment(p.alignment); },
    [&](const Instruction &, const Load &p) {
      alignment(p.alignment); require(p.address.type == Type::Ptr, "load address must be ptr");
    },
    [&](const Instruction &, const Store &p) {
      alignment(p.alignment); require(p.address.type == Type::Ptr, "store address must be ptr");
    },
    [&](const Instruction &, const Gep &p) {
      require(p.base.type == Type::Ptr && p.offset.type == Type::I32, "GEP requires ptr and i32 byte offset");
    },
    [&](const Instruction &, const Binary &p) {
      require(integer_width(p.result.type) && p.left.type == p.result.type &&
              p.right.type == p.result.type, "binary operands and result must share an integer type");
    },
    [&](const Instruction &, const Compare &p) {
      require(p.left.type == p.right.type, "comparison operand types differ");
      require(static_cast<unsigned>(p.predicate) <= static_cast<unsigned>(Predicate::UGe),
              "invalid comparison predicate");
      require(p.left.type != Type::Ptr || p.predicate == Predicate::Eq ||
              p.predicate == Predicate::Ne, "pointer comparisons require eq or ne");
    },
    [&](const Instruction &, const Cast &p) {
      auto from = integer_width(p.operand.type), to = integer_width(p.result.type);
      switch (p.op) {
      case CastOp::ZExt: require(from && to && from < to, "invalid zero extension"); break;
      case CastOp::Trunc: require(from && to && from > to, "invalid truncation"); break;
      case CastOp::PtrToInt:
        require(p.operand.type == Type::Ptr && p.result.type == Type::I32, "invalid RV32 pointer-to-int cast"); break;
      case CastOp::IntToPtr:
        require(p.operand.type == Type::I32 && p.result.type == Type::Ptr, "invalid RV32 int-to-pointer cast"); break;
      }
    },
    [&](const Instruction &, const Call &p) { require(!p.callee.empty(), "call requires a target"); },
    [&](const Instruction &, const Phi &p) {
      require(!p.incoming.empty(), "phi requires incoming values");
      std::set<BlockId> predecessors;
      for (const auto &incoming : p.incoming) {
        require(incoming.value.type == p.result.type, "phi incoming type mismatch");
        require(predecessors.insert(incoming.predecessor).second, "duplicate phi predecessor");
      }
    },
    [&](const Instruction &, const Branch &) {},
    [&](const Instruction &, const CondBranch &p) {
      require(p.condition.type == Type::I1, "branch condition must be i1");
    },
    [&](const Instruction &, const Return &) {},
    [&](const Instruction &, const Unreachable &) {}});
  return errors;
}
namespace {
struct Signature { Type result; std::vector<Type> parameters; };
using Signatures = std::map<std::string, Signature>;
void verify_function(const Function &, const Signatures &, std::vector<VerificationError> &) {}
} // namespace

std::vector<VerificationError> verify(const Module &module) {
  std::vector<VerificationError> errors;
  Signatures signatures;
  auto add = [&](const std::string &name, Type result, std::vector<Type> parameters) {
    auto error = [&](const char *message) { errors.push_back({name, {}, {}, message}); };
    if (name.empty()) error("function name is empty");
    if (result != Type::Void && !is_value_type(result)) error("invalid function result type");
    for (auto type : parameters)
      if (!is_value_type(type)) error("invalid function parameter type");
    if (!signatures.emplace(name, Signature{result, std::move(parameters)}).second)
      error("duplicate function name");
  };
  for (const auto &declaration : module.declarations)
    add(declaration.name, declaration.result, declaration.parameters);
  for (const auto &function : module.functions) {
    std::vector<Type> parameters;
    for (const auto &parameter : function.parameters) parameters.push_back(parameter.type);
    add(function.name, function.result, std::move(parameters));
  }
  for (const auto &function : module.functions) verify_function(function, signatures, errors);
  return errors;
}
} // namespace rx::ir
