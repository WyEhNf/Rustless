#pragma once

#include "rx/ir_ids.hpp"
#include "rx/semantic/symbol.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace rx::ir {

enum class Type { Void, I1, I8, I32, Ptr };

struct ConstantOperand {
  std::uint32_t bits;
};
struct LocalOperand {
  ValueId id;
};
struct GlobalOperand {
  std::string symbol;
};
struct Operand {
  using Data = std::variant<ConstantOperand, LocalOperand, GlobalOperand>;
  Operand(Type type, Data data) : type(type), data(std::move(data)) {}
  Type type;
  Data data;

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

struct Block {
  BlockId id{};
  std::string name;
  std::vector<Instruction> instructions;
};

struct Parameter {
  ValueId id{};
  Type type{Type::I32};
  std::string name;
  std::optional<SymbolId> symbol;
};

struct Function {
  std::string name;
  Type result{Type::Void};
  std::vector<Parameter> parameters;
  std::vector<Block> blocks;
  ValueId next_value{1};
  std::optional<SymbolId> symbol;
  std::optional<Origin> origin;
};

struct Declaration {
  std::string name;
  Type result{Type::Void};
  std::vector<Type> parameters;
  std::optional<SymbolId> symbol;
  std::optional<Origin> origin;
};

struct Module {
  std::vector<Declaration> declarations;
  std::vector<Function> functions;
};

bool is_terminator(Op op);
Op opcode(const Instruction &instruction);
std::optional<Result> definition(const Instruction &instruction);
bool is_terminator(const Instruction &instruction);
bool has_side_effects(const Instruction &instruction);
std::string type_name(Type type);
bool is_value_type(Type type);
unsigned integer_width(Type type);
std::string print(const Module &module);
std::string symbol_name(std::string_view name);

}
