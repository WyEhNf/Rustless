#pragma once

#include <array>
#include <string_view>

namespace rx {
enum class BuiltinId {
  GetInt,
  PrintInt,
  PrintlnInt,
  BoxNew,
  VecNew,
  Clone,
  Len,
  IsEmpty,
  Push,
  Remove
};
inline std::string_view builtin_name(BuiltinId id) {
  constexpr std::array<std::string_view, 10> names{
      "get_i32", "print_i32", "println_i32", "Box::new", "Vec::new",
      "clone",   "len",       "is_empty",    "push",     "remove"};
  return names.at(static_cast<std::size_t>(id));
}
}
