#pragma once

#include "rx/ast/path.hpp"

namespace rx::ast {
struct Expr;
using ExprPtr = Box<Expr>;
struct UnitType {};
struct PathType {
  Path path;
};
struct ReferenceType {
  std::optional<LifetimeName> lifetime;
  Mutability mutability;
  TypePtr referent;
};
struct ArrayType {
  TypePtr element;
  ExprPtr length;
};
struct ParenType {
  TypePtr inner;
};

struct TypeSyntax : Node {
  using Data =
      std::variant<UnitType, PathType, ReferenceType, ArrayType, ParenType>;
  explicit TypeSyntax(Data value) : data(std::move(value)) {}
  Data data;
};

template <class T> const T *get_if(const TypeSyntax &type) {
  return std::get_if<T>(&type.data);
}
} // namespace rx::ast
