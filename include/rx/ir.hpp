#pragma once

#include <cstdint>
#include <string>

namespace rx::ir {

enum class Type { Void, I1, I8, I32, Ptr };
using ValueId = std::uint32_t;
using BlockId = std::uint32_t;

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

} // namespace rx::ir
