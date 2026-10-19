#pragma once

#include "rx/llvm/ir/module.hpp"
#include <string_view>

namespace rx::ir {

bool is_terminator(Op op);
Op opcode(const Instruction &instruction);
std::optional<Result> definition(const Instruction &instruction);
bool is_terminator(const Instruction &instruction);
bool has_side_effects(const Instruction &instruction);
std::string print(const Module &module);
std::string symbol_name(std::string_view name);

}
