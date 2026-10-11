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
using Blocks = std::map<BlockId, const Block *>;
using Predecessors = std::map<BlockId, std::set<BlockId>>;
void verify_ssa(const Function &, const Blocks &, const Predecessors &,
                std::vector<VerificationError> &) {}
void verify_function(const Function &function, const Signatures &signatures,
                     std::vector<VerificationError> &errors) {
  auto error = [&](std::optional<BlockId> block, std::optional<Origin> origin,
                   std::string message) {
    errors.push_back({function.name, block, origin, std::move(message)});
  };
  if (function.blocks.empty()) { error({}, {}, "function has no entry block"); return; }
  Blocks blocks;
  Predecessors predecessors;
  for (const auto &block : function.blocks) {
    if (!blocks.emplace(block.id, &block).second) error(block.id, {}, "duplicate block identity");
    predecessors[block.id];
  }
  for (const auto &block : function.blocks) {
    bool terminated = false, non_phi = false;
    for (const auto &node : block.instructions) {
      for (auto message : verify_instruction(node)) error(block.id, node.origin, std::move(message));
      if (terminated) error(block.id, node.origin, "instruction follows a terminator");
      const bool phi = std::holds_alternative<Phi>(node.data);
      if (phi && non_phi) error(block.id, node.origin, "phi must precede ordinary instructions");
      non_phi |= !phi;
      terminated = std::holds_alternative<Branch>(node.data) ||
          std::holds_alternative<CondBranch>(node.data) ||
          std::holds_alternative<Return>(node.data) || std::holds_alternative<Unreachable>(node.data);
      if (std::holds_alternative<Alloca>(node.data) && block.id != function.blocks[0].id)
        error(block.id, node.origin, "fixed alloca must be in the entry block");
      for_each_successor(node, [&](BlockId target) {
        if (!blocks.contains(target)) error(block.id, node.origin, "branch targets a missing block");
        else predecessors[target].insert(block.id);
      });
      if (const auto *ret = std::get_if<Return>(&node.data)) {
        if ((!ret->value && function.result != Type::Void) ||
            (ret->value && ret->value->type != function.result))
          error(block.id, node.origin, "return does not match the function result");
      }
      if (const auto *call = std::get_if<Call>(&node.data)) {
        auto target = signatures.find(call->callee);
        if (target == signatures.end()) error(block.id, node.origin, "call target is undeclared");
        else {
          const auto &signature = target->second;
          if ((call->result ? call->result->type : Type::Void) != signature.result ||
              call->arguments.size() != signature.parameters.size())
            error(block.id, node.origin, "call signature mismatch");
          for (std::size_t i = 0; i < std::min(call->arguments.size(), signature.parameters.size()); ++i)
            if (call->arguments[i].type != signature.parameters[i])
              error(block.id, node.origin, "call argument type mismatch");
        }
      }
    }
    if (!terminated) error(block.id, {}, "block has no terminator");
  }
  if (!predecessors[function.blocks[0].id].empty()) error(function.blocks[0].id, {}, "entry block has predecessors");
  for (const auto &block : function.blocks)
    for (const auto &node : block.instructions)
      if (const auto *phi = std::get_if<Phi>(&node.data)) {
        std::set<BlockId> incoming;
        for (const auto &value : phi->incoming) incoming.insert(value.predecessor);
        if (incoming != predecessors[block.id]) error(block.id, node.origin, "phi predecessors do not match control flow");
      }
  verify_ssa(function, blocks, predecessors, errors);
}
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
