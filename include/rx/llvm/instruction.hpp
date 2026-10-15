#pragma once

#include "rx/llvm/operand.hpp"
#include <optional>
#include <vector>

namespace rx::ir {

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

struct Result {
  ValueId id;
  Type type;
};
struct Alloca {
  ValueId result;
  std::uint32_t bytes;
  std::uint32_t alignment{4};
};
struct Load {
  Result result;
  Operand address;
  std::uint32_t alignment{4};
};
struct Store {
  Operand value;
  Operand address;
  std::uint32_t alignment{4};
};
struct Gep {
  ValueId result;
  Operand base;
  Operand offset;
};

enum class BinaryOp { Add, Sub, Mul, SDiv, UDiv, SRem, URem, And, Or, Xor, Shl, LShr, AShr };
enum class CastOp { ZExt, Trunc, PtrToInt, IntToPtr };
struct Binary {
  Result result;
  BinaryOp op;
  Operand left;
  Operand right;
};
struct Compare {
  ValueId result;
  Predicate predicate;
  Operand left;
  Operand right;
};
struct Cast {
  Result result;
  CastOp op;
  Operand operand;
};
struct Call {

  std::optional<Result> result;
  std::string callee;
  std::vector<Operand> arguments;
};

struct PhiIncoming {
  Operand value;
  BlockId predecessor;
};
struct Phi {
  Result result;
  std::vector<PhiIncoming> incoming;
};
struct Branch {
  BlockId target;
};
struct CondBranch {
  Operand condition;
  BlockId consequent;
  BlockId alternative;
};
struct Return {
  std::optional<Operand> value;
};
struct Unreachable {};

struct Instruction {
  using Data = std::variant<Alloca, Load, Store, Gep, Binary, Compare, Cast, Call, Phi, Branch,
      CondBranch, Return, Unreachable>;
  explicit Instruction(Data data, std::optional<Origin> origin = {})
      : data(std::move(data)), origin(origin) {}
  Data data;
  std::optional<Origin> origin;
};

}
