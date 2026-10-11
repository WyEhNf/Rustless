#pragma once

#include "rx/ast/literal.hpp"
#include <algorithm>
#include <charconv>
#include <cstdint>
#include <optional>

namespace rx {
// Interpret the parser's typed literal descriptor rather than guessing its
// kind.
inline std::optional<std::uint64_t>
integer_magnitude(const ast::IntegerLiteral &literal) {
  std::string digits = literal.spelling;
  if (literal.suffix) {
    constexpr std::array suffixes{"i32", "u32", "isize", "usize"};
    auto index = static_cast<std::size_t>(*literal.suffix);
    if (index >= suffixes.size() || !digits.ends_with(suffixes[index]))
      return {};
    digits.resize(digits.size() - std::string_view(suffixes[index]).size());
  }
  digits.erase(std::remove(digits.begin(), digits.end(), '_'), digits.end());
  std::string_view prefix;
  switch (literal.base) {
  case ast::IntegerBase::Binary:
    prefix = "0b";
    break;
  case ast::IntegerBase::Octal:
    prefix = "0o";
    break;
  case ast::IntegerBase::Decimal:
    break;
  case ast::IntegerBase::Hex:
    prefix = "0x";
    break;
  default:
    return {};
  }
  if (!digits.starts_with(prefix) || digits.size() == prefix.size())
    return {};
  std::uint64_t value{};
  auto [end, error] = std::from_chars(digits.data() + prefix.size(),
                                      digits.data() + digits.size(), value,
                                      static_cast<int>(literal.base));
  if (error != std::errc{} || end != digits.data() + digits.size())
    return {};
  return value;
}
} // namespace rx
