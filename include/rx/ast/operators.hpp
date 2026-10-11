#pragma once

#include <array>
#include <stdexcept>
#include <string_view>

namespace rx::ast {
enum class BinaryOp {
  Add, Sub, Mul, Div, Rem, BitAnd, BitOr, BitXor, Shl, Shr,
  Eq, Ne, Lt, Le, Gt, Ge, LogicalAnd, LogicalOr
};
enum class UnaryOp { Negate, Not, Dereference };
enum class CompoundAssignOp {
  Add, Sub, Mul, Div, Rem, BitAnd, BitOr, BitXor, Shl, Shr
};

inline constexpr std::array binary_spellings{
  "+", "-", "*", "/", "%", "&", "|", "^", "<<", ">>",
  "==", "!=", "<", "<=", ">", ">=", "&&", "||"
};
inline constexpr std::array unary_spellings{"-", "!", "*"};
inline constexpr std::array compound_spellings{
  "+=", "-=", "*=", "/=", "%=", "&=", "|=", "^=", "<<=", ">>="
};

inline std::string_view spelling(BinaryOp op) {
  return binary_spellings.at(static_cast<std::size_t>(op));
}
inline std::string_view spelling(UnaryOp op) {
  return unary_spellings.at(static_cast<std::size_t>(op));
}
inline std::string_view spelling(CompoundAssignOp op) {
  return compound_spellings.at(static_cast<std::size_t>(op));
}

// Text conversion is used only at the parser boundary.
template <class Op, std::size_t N>
Op parse_operator(std::string_view text, const std::array<const char *, N> &names) {
  for (std::size_t i = 0; i < N; ++i)
    if (text == names[i])
      return static_cast<Op>(i);
  throw std::logic_error("unexpected operator spelling");
}
inline BinaryOp binary_operator(std::string_view text) {
  return parse_operator<BinaryOp>(text, binary_spellings);
}
inline UnaryOp unary_operator(std::string_view text) {
  return parse_operator<UnaryOp>(text, unary_spellings);
}
inline CompoundAssignOp compound_operator(std::string_view text) {
  return parse_operator<CompoundAssignOp>(text, compound_spellings);
}

enum class IntegerBase { Binary = 2, Octal = 8, Decimal = 10, Hex = 16 };
enum class IntegerSuffix { I32, U32, Isize, Usize };
enum class DeriveKind { Copy, Clone, PartialEq, Eq };
inline std::string_view spelling(DeriveKind kind) {
  constexpr std::array names{"Copy", "Clone", "PartialEq", "Eq"};
  return names.at(static_cast<std::size_t>(kind));
}
inline DeriveKind derive_kind(std::string_view text) {
  constexpr std::array names{"Copy", "Clone", "PartialEq", "Eq"};
  return parse_operator<DeriveKind>(text, names);
}
} // namespace rx::ast
