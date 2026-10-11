#pragma once

#include "rx/ast/expr.hpp"

namespace rx::ast {
enum class Termination { Semicolon, Omitted };
struct EmptyStmt {};
struct LetStmt {
  Binding binding;
  std::optional<TypePtr> annotation;
  ExprPtr initializer;
};
struct ExprStmt {
  ExprPtr expression;
  Termination termination;
};
struct Stmt : Node {
  using Data = std::variant<EmptyStmt, LetStmt, ExprStmt>;
  explicit Stmt(Data value) : data(std::move(value)) {}
  Data data;
};
using StmtPtr = Box<Stmt>;
struct Block : Node {
  std::vector<StmtPtr> statements;
  std::optional<ExprPtr> tail;
};
template <class T> const T *get_if(const Stmt &stmt) {
  return std::get_if<T>(&stmt.data);
}
} // namespace rx::ast
