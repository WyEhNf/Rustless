#pragma once

#include "rx/llvm/verify.hpp"

namespace rx::ir {

class Builder {
public:
  explicit Builder(Function &function);
  BlockId create_block(std::string name);
  void select(BlockId block);
  void set_origin(const ast::Node &node) { origin_ = ir::origin(node); }
  void clear_origin() { origin_.reset(); }
  Operand allocate(std::uint32_t bytes, std::uint32_t alignment = 4);
  Operand load(Type type, Operand address, std::uint32_t alignment = 4);
  void store(Operand value, Operand address, std::uint32_t alignment = 4);
  Operand gep(Operand base, Operand offset);
  Operand binary(BinaryOp op, Operand left, Operand right);
  Operand compare(Predicate predicate, Operand left, Operand right);
  Operand cast(CastOp op, Type target, Operand operand);
  std::optional<Operand> call(Type result, std::string callee, std::vector<Operand> arguments);
  Operand phi(Type type, std::vector<PhiIncoming> incoming);
  void branch(BlockId target);
  void cond_branch(Operand condition, BlockId consequent, BlockId alternative);
  void ret(std::optional<Operand> value = {});
  void unreachable();

private:
  Result fresh(Type type);
  Block &current();
  void append(Instruction::Data data);
  Function &function_;
  std::optional<BlockId> current_;
  std::optional<Origin> origin_;
  std::uint64_t next_block_{};
};
}
