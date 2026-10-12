#include "rx/ir_verify.hpp"
#include <algorithm>
#include <iterator>
#include <map>
#include <set>

namespace rx::ir {
namespace {
bool same_operand(const Operand &left, const Operand &right) {
  if (left.type != right.type || left.data.index() != right.data.index())
    return false;
  return visit(left, ast::Overloaded{
                         [&](const Operand &, const ConstantOperand &p) {
                           return p.bits ==
                                  std::get<ConstantOperand>(right.data).bits;
                         },
                         [&](const Operand &, const LocalOperand &p) {
                           return p.id == std::get<LocalOperand>(right.data).id;
                         },
                         [&](const Operand &, const GlobalOperand &p) {
                           return p.symbol ==
                                  std::get<GlobalOperand>(right.data).symbol;
                         }});
}
} // namespace
std::vector<std::string> verify_instruction(const Instruction &node) {
  std::vector<std::string> errors;
  auto require = [&](bool ok, const char *message) {
    if (!ok)
      errors.emplace_back(message);
  };
  if (node.data.valueless_by_exception())
    return {"instruction has no payload"};
  try {
    (void)opcode(node);
  } catch (const std::exception &) {
    errors.emplace_back("invalid instruction operator");
  }
  if (auto result = definition(node))
    require(result->id.value && is_value_type(result->type),
            "invalid SSA result");
  if (node.origin)
    require(node.origin->node.value && node.origin->span.file.value &&
                node.origin->span.begin <= node.origin->span.end,
            "invalid syntax provenance");
  for_each_operand(node, [&](const Operand &operand) {
    require(is_value_type(operand.type), "operand has an invalid value type");
    visit(operand,
          ast::Overloaded{
              [&](const Operand &owner, const ConstantOperand &p) {
                require((owner.type != Type::I1 || p.bits <= 1) &&
                            (owner.type != Type::I8 || p.bits <= 255) &&
                            (owner.type != Type::Ptr || p.bits == 0),
                        "constant does not fit its type");
              },
              [&](const Operand &, const LocalOperand &p) {
                require(p.id.value != 0, "zero SSA operand");
              },
              [&](const Operand &owner, const GlobalOperand &p) {
                require(owner.type == Type::Ptr && !p.symbol.empty(),
                        "invalid global operand");
              }});
  });
  auto alignment = [&](std::uint32_t value) {
    require(value && !(value & (value - 1)),
            "alignment must be a nonzero power of two");
  };
  visit(
      node,
      ast::Overloaded{
          [&](const Instruction &, const Alloca &p) { alignment(p.alignment); },
          [&](const Instruction &, const Load &p) {
            alignment(p.alignment);
            require(p.address.type == Type::Ptr, "load address must be ptr");
          },
          [&](const Instruction &, const Store &p) {
            alignment(p.alignment);
            require(p.address.type == Type::Ptr, "store address must be ptr");
          },
          [&](const Instruction &, const Gep &p) {
            require(p.base.type == Type::Ptr && p.offset.type == Type::I32,
                    "GEP requires ptr and i32 byte offset");
          },
          [&](const Instruction &, const Binary &p) {
            require(integer_width(p.result.type) &&
                        p.left.type == p.result.type &&
                        p.right.type == p.result.type,
                    "binary operands and result must share an integer type");
          },
          [&](const Instruction &, const Compare &p) {
            require(p.left.type == p.right.type,
                    "comparison operand types differ");
            require(static_cast<unsigned>(p.predicate) <=
                        static_cast<unsigned>(Predicate::UGe),
                    "invalid comparison predicate");
            require(p.left.type != Type::Ptr || p.predicate == Predicate::Eq ||
                        p.predicate == Predicate::Ne,
                    "pointer comparisons require eq or ne");
          },
          [&](const Instruction &, const Cast &p) {
            auto from = integer_width(p.operand.type),
                 to = integer_width(p.result.type);
            switch (p.op) {
            case CastOp::ZExt:
              require(from && to && from < to, "invalid zero extension");
              break;
            case CastOp::Trunc:
              require(from && to && from > to, "invalid truncation");
              break;
            case CastOp::PtrToInt:
              require(p.operand.type == Type::Ptr && p.result.type == Type::I32,
                      "invalid RV32 pointer-to-int cast");
              break;
            case CastOp::IntToPtr:
              require(p.operand.type == Type::I32 && p.result.type == Type::Ptr,
                      "invalid RV32 int-to-pointer cast");
              break;
            }
          },
          [&](const Instruction &, const Call &p) {
            require(!p.callee.empty(), "call requires a target");
          },
          [&](const Instruction &, const Phi &p) {
            require(!p.incoming.empty(), "phi requires incoming values");
            std::map<BlockId, const Operand *> values;
            for (const auto &incoming : p.incoming) {
              require(incoming.value.type == p.result.type,
                      "phi incoming type mismatch");
              auto [position, inserted] =
                  values.emplace(incoming.predecessor, &incoming.value);
              require(inserted ||
                          same_operand(*position->second, incoming.value),
                      "parallel phi edges must carry the same value");
            }
          },
          [&](const Instruction &, const Branch &) {},
          [&](const Instruction &, const CondBranch &p) {
            require(p.condition.type == Type::I1,
                    "branch condition must be i1");
          },
          [&](const Instruction &, const Return &) {},
          [&](const Instruction &, const Unreachable &) {}});
  return errors;
}
namespace {
struct Signature {
  Type result;
  std::vector<Type> parameters;
};
using Signatures = std::map<std::string, Signature>;
using Blocks = std::map<BlockId, const Block *>;
using Predecessors = std::map<BlockId, std::set<BlockId>>;
using IncomingEdges = std::map<BlockId, std::multiset<BlockId>>;
using Dominators = std::map<BlockId, std::set<BlockId>>;
Dominators dominators(const Function &function, const Blocks &blocks,
                      const Predecessors &predecessors) {
  const auto entry = function.blocks.front().id;
  std::set<BlockId> reachable;
  std::vector<BlockId> pending{entry};
  while (!pending.empty()) {
    auto id = pending.back();
    pending.pop_back();
    if (!reachable.insert(id).second)
      continue;
    for_each_successor(blocks.at(id)->instructions.back(),
                       [&](BlockId target) { pending.push_back(target); });
  }
  Dominators result;
  for (const auto &[id, _] : blocks)
    result[id] =
        id == entry || !reachable.contains(id) ? std::set{id} : reachable;
  bool changed = true;
  while (changed) {
    changed = false;
    for (auto id : reachable) {
      if (id == entry)
        continue;
      auto next = reachable;
      for (auto predecessor : predecessors.at(id)) {
        if (!reachable.contains(predecessor))
          continue;
        std::set<BlockId> intersection;
        std::set_intersection(next.begin(), next.end(),
                              result.at(predecessor).begin(),
                              result.at(predecessor).end(),
                              std::inserter(intersection, intersection.end()));
        next = std::move(intersection);
      }
      next.insert(id);
      if (next != result.at(id)) {
        result[id] = std::move(next);
        changed = true;
      }
    }
  }
  return result;
}
void verify_ssa(const Function &function, const Blocks &blocks,
                const Predecessors &predecessors,
                std::vector<VerificationError> &errors) {
  struct Definition {
    Type type;
    std::optional<BlockId> block;
    std::size_t position;
  };
  std::map<ValueId, Definition> definitions;
  auto error = [&](std::optional<BlockId> block, std::optional<Origin> origin,
                   const char *message) {
    errors.push_back({function.name, block, origin, message});
  };
  auto define = [&](ValueId id, Type type, std::optional<BlockId> block,
                    std::size_t position) {
    if (!id.value ||
        !definitions.emplace(id, Definition{type, block, position}).second)
      error(block, {}, "duplicate or zero SSA definition");
  };
  for (const auto &parameter : function.parameters)
    define(parameter.id, parameter.type, {}, 0);
  for (const auto &block : function.blocks)
    for (std::size_t i = 0; i < block.instructions.size(); ++i)
      if (auto result = definition(block.instructions[i]))
        define(result->id, result->type, block.id, i);
  const auto dom = dominators(function, blocks, predecessors);
  auto use = [&](const Operand &operand, BlockId use_block,
                 std::size_t position, BlockId report_block,
                 const Instruction &node) {
    if (const auto *local = std::get_if<LocalOperand>(&operand.data)) {
      auto definition = definitions.find(local->id);
      if (definition == definitions.end()) {
        error(report_block, node.origin, "undefined SSA operand");
        return;
      }
      const auto &value = definition->second;
      if (value.type != operand.type)
        error(report_block, node.origin,
              "SSA operand type differs from its definition");
      if (value.block) {
        if (*value.block == use_block && value.position >= position)
          error(report_block, node.origin,
                "SSA value is used before its definition");
        else if (!dom.at(use_block).contains(*value.block))
          error(report_block, node.origin,
                "SSA definition does not dominate its use");
      }
    }
  };
  for (const auto &block : function.blocks)
    for (std::size_t i = 0; i < block.instructions.size(); ++i) {
      const auto &node = block.instructions[i];
      if (const auto *phi = std::get_if<Phi>(&node.data)) {
        for (const auto &incoming : phi->incoming)
          use(incoming.value, incoming.predecessor,
              blocks.at(incoming.predecessor)->instructions.size() - 1,
              block.id, node);
      } else
        for_each_operand(node, [&](const Operand &operand) {
          use(operand, block.id, i, block.id, node);
        });
    }
}
void verify_function(const Function &function, const Signatures &signatures,
                     std::vector<VerificationError> &errors) {
  const auto initial_errors = errors.size();
  auto error = [&](std::optional<BlockId> block, std::optional<Origin> origin,
                   std::string message) {
    errors.push_back({function.name, block, origin, std::move(message)});
  };
  if (function.blocks.empty()) {
    error({}, {}, "function has no entry block");
    return;
  }
  Blocks blocks;
  Predecessors predecessors;
  IncomingEdges incoming_edges;
  for (const auto &block : function.blocks) {
    if (!blocks.emplace(block.id, &block).second)
      error(block.id, {}, "duplicate block identity");
    predecessors[block.id];
    incoming_edges[block.id];
  }
  for (const auto &block : function.blocks) {
    bool terminated = false, non_phi = false;
    for (const auto &node : block.instructions) {
      for (auto message : verify_instruction(node))
        error(block.id, node.origin, std::move(message));
      for_each_operand(node, [&](const Operand &operand) {
        if (const auto *global = std::get_if<GlobalOperand>(&operand.data))
          if (!signatures.contains(global->symbol))
            error(block.id, node.origin,
                  "global operand refers to an undeclared symbol");
      });
      if (terminated)
        error(block.id, node.origin, "instruction follows a terminator");
      const bool phi = std::holds_alternative<Phi>(node.data);
      if (phi && non_phi)
        error(block.id, node.origin, "phi must precede ordinary instructions");
      non_phi |= !phi;
      terminated = std::holds_alternative<Branch>(node.data) ||
                   std::holds_alternative<CondBranch>(node.data) ||
                   std::holds_alternative<Return>(node.data) ||
                   std::holds_alternative<Unreachable>(node.data);
      if (std::holds_alternative<Alloca>(node.data) &&
          block.id != function.blocks[0].id)
        error(block.id, node.origin, "fixed alloca must be in the entry block");
      for_each_successor(node, [&](BlockId target) {
        if (!blocks.contains(target))
          error(block.id, node.origin, "branch targets a missing block");
        else {
          predecessors[target].insert(block.id);
          incoming_edges[target].insert(block.id);
        }
      });
      if (const auto *ret = std::get_if<Return>(&node.data)) {
        if ((!ret->value && function.result != Type::Void) ||
            (ret->value && ret->value->type != function.result))
          error(block.id, node.origin,
                "return does not match the function result");
      }
      if (const auto *call = std::get_if<Call>(&node.data)) {
        auto target = signatures.find(call->callee);
        if (target == signatures.end())
          error(block.id, node.origin, "call target is undeclared");
        else {
          const auto &signature = target->second;
          if ((call->result ? call->result->type : Type::Void) !=
                  signature.result ||
              call->arguments.size() != signature.parameters.size())
            error(block.id, node.origin, "call signature mismatch");
          for (std::size_t i = 0; i < std::min(call->arguments.size(),
                                               signature.parameters.size());
               ++i)
            if (call->arguments[i].type != signature.parameters[i])
              error(block.id, node.origin, "call argument type mismatch");
        }
      }
    }
    if (!terminated)
      error(block.id, {}, "block has no terminator");
  }
  if (!predecessors[function.blocks[0].id].empty())
    error(function.blocks[0].id, {}, "entry block has predecessors");
  for (const auto &block : function.blocks)
    for (const auto &node : block.instructions)
      if (const auto *phi = std::get_if<Phi>(&node.data)) {
        std::multiset<BlockId> incoming;
        for (const auto &value : phi->incoming)
          incoming.insert(value.predecessor);
        if (incoming != incoming_edges[block.id])
          error(block.id, node.origin,
                "phi incoming edges do not match control flow");
      }
  if (errors.size() == initial_errors)
    verify_ssa(function, blocks, predecessors, errors);
}
} // namespace

std::vector<VerificationError> verify(const Module &module) {
  std::vector<VerificationError> errors;
  Signatures signatures;
  auto metadata = [&](const std::string &name, std::optional<SymbolId> symbol,
                      std::optional<Origin> origin) {
    if (symbol && !symbol->value)
      errors.push_back({name, {}, origin, "zero semantic symbol identity"});
    if (origin && (!origin->node.value || !origin->span.file.value ||
                   origin->span.begin > origin->span.end))
      errors.push_back(
          {name, {}, origin, "invalid declaration syntax provenance"});
  };
  auto add = [&](const std::string &name, Type result,
                 std::vector<Type> parameters) {
    auto error = [&](const char *message) {
      errors.push_back({name, {}, {}, message});
    };
    if (name.empty())
      error("function name is empty");
    if (result != Type::Void && !is_value_type(result))
      error("invalid function result type");
    for (auto type : parameters)
      if (!is_value_type(type))
        error("invalid function parameter type");
    if (!signatures.emplace(name, Signature{result, std::move(parameters)})
             .second)
      error("duplicate function name");
  };
  for (const auto &declaration : module.declarations) {
    add(declaration.name, declaration.result, declaration.parameters);
    metadata(declaration.name, declaration.symbol, declaration.origin);
  }
  for (const auto &function : module.functions) {
    std::vector<Type> parameters;
    for (const auto &parameter : function.parameters) {
      parameters.push_back(parameter.type);
      metadata(function.name, parameter.symbol, {});
    }
    add(function.name, function.result, std::move(parameters));
    metadata(function.name, function.symbol, function.origin);
  }
  for (const auto &function : module.functions)
    verify_function(function, signatures, errors);
  return errors;
}
} // namespace rx::ir
