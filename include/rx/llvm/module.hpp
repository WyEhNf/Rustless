#pragma once

#include "rx/llvm/instruction.hpp"
#include "rx/semantic/symbol.hpp"

namespace rx::ir {

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

}
