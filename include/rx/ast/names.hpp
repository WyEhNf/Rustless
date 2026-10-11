#pragma once

#include "rx/ast/node.hpp"
#include <string>

namespace rx::ast {
struct Identifier { std::string text; Span span; };
struct LifetimeName { std::string text; Span span; };
enum class Mutability { Immutable, Mutable };
struct Binding {
  Identifier name;
  Mutability mutability;
};
} // namespace rx::ast
