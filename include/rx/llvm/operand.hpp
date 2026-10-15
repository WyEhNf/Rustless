#pragma once

#include "rx/llvm/ids.hpp"
#include "rx/llvm/type.hpp"
#include <variant>

namespace rx::ir {

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

}
