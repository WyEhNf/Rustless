#pragma once

#include "rx/ast/literal.hpp"
#include "rx/ast/type.hpp"

namespace rx::ast {
struct Block;
struct IfBranch;
struct PathExpr {
  Path path;
};
struct ParenExpr {
  ExprPtr inner;
};
struct ArrayExpr {
  std::vector<ExprPtr> elements;
};
struct RepeatArrayExpr {
  ExprPtr element;
  ExprPtr count;
};
struct FieldInit : Node {
  Identifier name;
  ExprPtr value;
};
struct StructExpr {
  Path path;
  std::vector<FieldInit> fields;
};
struct BlockExpr {
  Box<Block> block;
};
using ElseBranch = std::variant<Box<Block>, Box<IfBranch>>;
struct IfExpr {
  ExprPtr condition;
  Box<Block> then_block;
  std::optional<ElseBranch> else_branch;
};
struct IfBranch : Node {
  explicit IfBranch(IfExpr value) : data(std::move(value)) {}
  IfExpr data;
};
struct LoopExpr {
  Box<Block> body;
};
struct WhileExpr {
  ExprPtr condition;
  Box<Block> body;
};
struct UnaryExpr {
  UnaryOp op;
  Span operator_span;
  ExprPtr operand;
};
struct BorrowExpr {
  Mutability mutability;
  Span operator_span;
  ExprPtr operand;
};
struct BinaryExpr {
  BinaryOp op;
  Span operator_span;
  ExprPtr left;
  ExprPtr right;
};
struct AssignExpr {
  ExprPtr target;
  ExprPtr value;
};
struct CompoundAssignExpr {
  CompoundAssignOp op;
  Span operator_span;
  ExprPtr target;
  ExprPtr value;
};
struct CastExpr {
  ExprPtr value;
  TypePtr target_type;
};
struct CallExpr {
  ExprPtr callee;
  std::vector<ExprPtr> arguments;
};
struct MethodCallExpr {
  ExprPtr receiver;
  Identifier method;
  std::optional<GenericArguments> generic_args;
  std::vector<ExprPtr> arguments;
};
struct FieldExpr {
  ExprPtr base;
  Identifier field;
};
struct IndexExpr {
  ExprPtr base;
  ExprPtr index;
};
struct BreakExpr {
  std::optional<ExprPtr> value;
};
struct ContinueExpr {};
struct ReturnExpr {
  std::optional<ExprPtr> value;
};

struct Expr : Node {
  using Data =
      std::variant<IntegerExpr, BooleanExpr, UnitExpr, PathExpr, ParenExpr,
                   ArrayExpr, RepeatArrayExpr, StructExpr, BlockExpr, IfExpr,
                   LoopExpr, WhileExpr, UnaryExpr, BorrowExpr, BinaryExpr,
                   AssignExpr, CompoundAssignExpr, CastExpr, CallExpr,
                   MethodCallExpr, FieldExpr, IndexExpr, BreakExpr,
                   ContinueExpr, ReturnExpr>;
  explicit Expr(Data value) : data(std::move(value)) {}
  Data data;
};
template <class Payload> const Payload *get_if(const Expr &expr) {
  return std::get_if<Payload>(&expr.data);
}
inline const ExprPtr &unparen(const ExprPtr &expr) {
  if (auto *paren = get_if<ParenExpr>(*expr))
    return unparen(paren->inner);
  return expr;
}
}
