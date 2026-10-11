#pragma once

#include "rx/ast/operators.hpp"
#include <optional>
#include <string>

namespace rx::ast {
struct IntegerLiteral {
  std::string spelling;
  IntegerBase base;
  std::optional<IntegerSuffix> suffix;

  static IntegerLiteral parse(std::string text) {
    IntegerBase base = IntegerBase::Decimal;
    if (text.starts_with("0b")) base = IntegerBase::Binary;
    else if (text.starts_with("0o")) base = IntegerBase::Octal;
    else if (text.starts_with("0x")) base = IntegerBase::Hex;
    std::optional<IntegerSuffix> suffix;
    constexpr std::array names{"i32", "u32", "isize", "usize"};
    for (std::size_t i = 0; i < names.size(); ++i)
      if (text.ends_with(names[i])) suffix = static_cast<IntegerSuffix>(i);
    return {std::move(text), base, suffix};
  }
};
struct IntegerExpr { IntegerLiteral literal; };
struct BooleanExpr { bool value; };
struct UnitExpr {};
} // namespace rx::ast
