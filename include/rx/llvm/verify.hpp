#pragma once

#include "rx/llvm/visit.hpp"

namespace rx::ir {
struct VerificationError {
  std::string function;
  std::optional<BlockId> block;
  std::optional<Origin> origin;
  std::string message;
};
std::vector<std::string> verify_instruction(const Instruction &instruction);
std::vector<VerificationError> verify(const Module &module);
}
