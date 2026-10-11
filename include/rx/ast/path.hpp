#pragma once

#include "rx/ast/names.hpp"
#include <optional>
#include <variant>
#include <vector>

namespace rx::ast {
struct TypeSyntax;
using TypePtr = Box<TypeSyntax>;
struct SelfValue { Span span; };
struct SelfType { Span span; };
using PathName = std::variant<Identifier, SelfValue, SelfType>;
using GenericArgument = std::variant<TypePtr, LifetimeName>;
struct GenericArguments { Span span; std::vector<GenericArgument> values; };
struct PathSegment {
  PathName name;
  std::optional<GenericArguments> arguments;
  Span span;
};
struct Path { Span span; std::vector<PathSegment> segments; };

inline std::string_view name_text(const PathName &name) {
  return std::visit(Overloaded{
    [](const Identifier &name) -> std::string_view { return name.text; },
    [](const SelfValue &) -> std::string_view { return "self"; },
    [](const SelfType &) -> std::string_view { return "Self"; }
  }, name);
}
inline std::string path_name(const Path &path) {
  std::string result;
  for (const auto &segment : path.segments) {
    if (!result.empty()) result += "::";
    result += name_text(segment.name);
  }
  return result;
}
} // namespace rx::ast
