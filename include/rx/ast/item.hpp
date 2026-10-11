#pragma once

#include "rx/ast/generics.hpp"
#include "rx/ast/source.hpp"
#include "rx/ast/stmt.hpp"
#include "rx/ast/use.hpp"

namespace rx::ast {
struct ValueSelf {
  Mutability mutability;
};
struct ReferenceSelf {
  Mutability mutability;
  std::optional<LifetimeName> lifetime;
};
struct SelfParam : Node {
  std::variant<ValueSelf, ReferenceSelf> data;
};
inline bool by_reference(const SelfParam &self) {
  return std::holds_alternative<ReferenceSelf>(self.data);
}
inline bool is_mutable(const SelfParam &self) {
  return std::visit(
      [](const auto &value) { return value.mutability == Mutability::Mutable; },
      self.data);
}
struct Parameter : Node {
  Binding binding;
  TypePtr type;
};
struct FunctionSignature {
  std::optional<SelfParam> receiver;
  std::vector<Parameter> parameters;
  std::optional<TypePtr> return_annotation;
};
struct Function : Node {
  Identifier name;
  std::optional<GenericParameters> generics;
  FunctionSignature signature;
  std::optional<WhereClause> where_clause;
  Box<Block> body;
};
struct DeriveName {
  DeriveKind kind;
  Span span;
};
struct DeriveAttribute {
  Span span;
  std::vector<DeriveName> names;
};
struct StructField : Node {
  Identifier name;
  TypePtr type;
};
struct Struct : Node {
  Identifier name;
  std::vector<DeriveAttribute> attributes;
  std::optional<GenericParameters> generics;
  std::optional<WhereClause> where_clause;
  std::vector<StructField> fields;
};
struct Constant : Node {
  Identifier name;
  TypePtr type;
  ExprPtr value;
};
using ImplMember = std::variant<Function, Constant>;
struct Impl : Node {
  std::optional<GenericParameters> generics;
  TypePtr target;
  std::optional<WhereClause> where_clause;
  std::vector<ImplMember> members;
};
using Item = std::variant<Use, Function, Struct, Constant, Impl>;
struct Crate : Node {
  explicit Crate(std::string source) : source(std::move(source)) {}
  SourceFile source;
  std::vector<Item> items;
};

std::string dump(const Crate &crate);
} // namespace rx::ast
