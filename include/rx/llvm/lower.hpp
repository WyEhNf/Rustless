#pragma once

#include "rx/llvm/ir/ir.hpp"
#include "rx/semantic/semantic.hpp"

namespace rx::ir {

Module lower(const ast::Crate &crate, const SemanticResult &semantic);

}
