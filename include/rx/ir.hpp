#pragma once

#include "rx/ir_ids.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace rx::ir {

enum class Type { Void, I1, I8, I32, Ptr };

struct Operand {
  Type type{Type::Void};
  enum class Kind { Constant, Local, Global } kind{Kind::Constant};
  std::uint32_t value{};
  std::string symbol;

  static Operand constant(Type type, std::uint32_t bits);
  static Operand local(Type type, ValueId id);
  static Operand global(std::string name);
};

enum class Op {
  Alloca,
  Load,
  Store,
  Gep,
  Add,
  Sub,
  Mul,
  SDiv,
  UDiv,
  SRem,
  URem,
  And,
  Or,
  Xor,
  Shl,
  LShr,
  AShr,
  ICmp,
  ZExt,
  Trunc,
  PtrToInt,
  IntToPtr,
  Call,
  Phi,
  Br,
  CondBr,
  Ret,
  Unreachable
};

enum class Predicate { Eq, Ne, SLt, SLe, SGt, SGe, ULt, ULe, UGt, UGe };

struct Instruction {
  Op op{Op::Unreachable};
  ValueId result{}; // Zero means no result; SSA IDs start at one.
  Type type{Type::Void};
  std::vector<Operand> operands;
  std::vector<BlockId> targets; // Phi predecessors or branch successors.
  std::string callee;
  Predicate predicate{Predicate::Eq};
  std::uint32_t bytes{}; // Fixed-size entry-block alloca.
  std::uint32_t alignment{4};
};

struct Block {
  BlockId id{};
  std::string name;
  std::vector<Instruction> instructions;
};

struct Parameter {
  ValueId id{};
  Type type{Type::I32};
  std::string name;
};

struct Function {
  std::string name;
  Type result{Type::Void};
  std::vector<Parameter> parameters;
  std::vector<Block> blocks;
  ValueId next_value{1};
};

struct Declaration {
  std::string name;
  Type result{Type::Void};
  std::vector<Type> parameters;
};

struct Module {
  std::vector<Declaration> declarations;
  std::vector<Function> functions;
};

bool is_terminator(Op op);
bool has_side_effects(const Instruction &instruction);
std::string type_name(Type type);
std::string print(const Module &module);

} // namespace rx::ir
