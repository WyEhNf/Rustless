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
    [&](const Instruction &, const auto &) {}});
  return errors;
}
// Module-level validation follows after instruction contracts.
std::vector<VerificationError> verify(const Module &) { return {}; }
} // namespace rx::ir
