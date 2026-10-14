#pragma once

#include "rx/ast/literal.hpp"
#include <algorithm>
#include <array>
#include <charconv>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace rx {
inline std::optional<std::uint64_t>
integer_magnitude(const ast::IntegerLiteral &literal) {
  std::string digits = literal.spelling;
  if (literal.suffix) {
    constexpr std::array<std::string_view, 4> suffixes{"i32", "u32", "isize",
                                                       "usize"};
    const auto suffix_index = static_cast<std::size_t>(*literal.suffix);
    if (suffix_index >= suffixes.size())
      return {};
    const std::string_view suffix = suffixes[suffix_index];
    if (digits.size() < suffix.size())
      return {};
    const std::size_t suffix_begin = digits.size() - suffix.size();
    if (digits.compare(suffix_begin, suffix.size(), suffix) != 0)
      return {};
    digits.resize(suffix_begin);
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
  if (digits.compare(0, prefix.size(), prefix) != 0 ||
      digits.size() == prefix.size())
    return {};
  std::uint64_t value{};
  const char *number_begin = digits.data() + prefix.size();
  const char *number_end = digits.data() + digits.size();
  const auto parsed = std::from_chars(number_begin, number_end, value,
                                      static_cast<int>(literal.base));
  if (parsed.ec != std::errc{} || parsed.ptr != number_end)
    return {};
  return value;
}
} // namespace rx
