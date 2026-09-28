#pragma once

#include "rx/diagnostic.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace rx::ast {

// Stable identity and source location shared by every syntax node.
using NodeId = std::uint32_t;

struct Node {
  NodeId id{};
  Span span{};
};

struct Type;
using TypePtr = std::shared_ptr<Type>;

struct Type : Node {
  // Types retain source spelling until semantic resolution.
  enum class Kind { Unit, Path, Reference, Array } kind{Kind::Unit};
  std::vector<std::string> path;
  std::vector<TypePtr> arguments;
  TypePtr element;
  bool is_mutable{};
  std::string array_length;
};

struct Expr;
using ExprPtr = std::shared_ptr<Expr>;
struct Stmt;

struct FieldInit {
  std::string name;
  ExprPtr value;
  Span span{};
};

struct Expr : Node {
  // Operands follow source order for every expression kind.
  enum class Kind {
    Integer,
    Boolean,
    Unit,
    Path,
    Array,
    Struct,
    Block,
    If,
    Loop,
    While,
    Unary,
    Binary,
    Assign,
    Cast,
    Call,
    MethodCall,
    Field,
    Index,
    Break,
    Continue,
    Return
  } kind{Kind::Unit};
  std::string text;
  std::vector<std::string> path;
  std::vector<TypePtr> type_arguments;
  std::vector<ExprPtr> operands;
  std::vector<FieldInit> fields;
  std::vector<std::shared_ptr<Stmt>> statements;
  TypePtr cast_type;
  bool flag{};
};

struct Stmt : Node {
  enum class Kind { Empty, Let, Expr } kind{Kind::Empty};
  std::string name;
  bool is_mutable{};
  TypePtr annotation;
  ExprPtr expression;
  bool has_semicolon{};
};

struct Parameter : Node {
  std::string name;
  bool is_mutable{};
  TypePtr type;
};

struct Function : Node {
  std::string name;
  bool has_self{};
  bool self_by_ref{};
  bool self_mutable{};
  std::vector<std::string> lifetime_parameters;
  std::vector<Parameter> parameters;
  TypePtr result;
  ExprPtr body;
};

struct StructField : Node {
  std::string name;
  TypePtr type;
};

struct Struct : Node {
  std::string name;
  std::vector<std::string> derives;
  std::vector<std::string> lifetime_parameters;
  std::vector<StructField> fields;
};

struct Constant : Node {
  std::string name;
  TypePtr type;
  ExprPtr value;
};

struct Impl : Node {
  TypePtr target;
  std::vector<std::string> lifetime_parameters;
  std::vector<Function> functions;
  std::vector<Constant> constants;
};

struct Use : Node {
  std::string text;
};

using Item = std::variant<Use, Function, Struct, Constant, Impl>;

struct Crate : Node {
  // Top-level items stay in source order for deterministic diagnostics.
  std::vector<Item> items;
};

std::string dump(const Crate &crate);

} // namespace rx::ast
