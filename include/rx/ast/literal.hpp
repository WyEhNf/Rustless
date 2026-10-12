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
    const std::string prefix = text.substr(0, 2);
    if (prefix == "0b") {
      base = IntegerBase::Binary;
    } else if (prefix == "0o") {
      base = IntegerBase::Octal;
    } else if (prefix == "0x") {
      base = IntegerBase::Hex;
    }
    std::optional<IntegerSuffix> suffix;
    constexpr std::array<std::string_view, 4> names{"i32", "u32", "isize",
                                                    "usize"};
    for (std::size_t index = 0; index < names.size(); ++index) {
      const std::string_view candidate = names[index];
      if (text.size() < candidate.size()) {
        continue;
      }
      const std::size_t suffix_start = text.size() - candidate.size();
      if (text.compare(suffix_start, candidate.size(), candidate) == 0) {
        suffix = static_cast<IntegerSuffix>(index);
      }
    }
    return {std::move(text), base, suffix};
  }
};
struct IntegerExpr {
  IntegerLiteral literal;
};
struct BooleanExpr {
  bool value;
};
struct UnitExpr {};
}
