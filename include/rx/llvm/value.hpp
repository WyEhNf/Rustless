#pragma once

#include "rx/llvm/operand.hpp"
#include "rx/semantic/type.hpp"

namespace rx::ir {

struct UnitValue {};
struct ScalarValue {
  Operand operand;
};
struct StoredValue {
  Operand address;
};
struct Value {
  SemanticTypePtr type;
  std::variant<UnitValue, ScalarValue, StoredValue> data;
};

}
