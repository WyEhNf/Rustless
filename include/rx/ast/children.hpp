#pragma once

#include "rx/ast/dispatch.hpp"

namespace rx::ast {
namespace detail {
// No catch-all: every new payload must describe its structural children.
template <class Recurse> class ChildEnumerator {
public:
  explicit ChildEnumerator(Recurse &recurse) : recurse_(recurse) {}
  void operator()(const Crate &p) {
    for (const auto &item : p.items)
      dispatch(item, [&](const auto &owner, const auto &) { recurse_(owner); });
  }
  void operator()(const Block &p) {
    for (const auto &statement : p.statements)
      recurse_(*statement);
    if (p.tail)
      recurse_(**p.tail);
  }
  void operator()(const Use &p) { recurse_(p.tree); }
  void operator()(const NameImport &) {}
  void operator()(const GlobImport &) {}
  void operator()(const GroupImport &p) {
    for (const auto &child : p.children)
      recurse_(child);
  }
  void operator()(const ValueSelf &) {}
  void operator()(const ReferenceSelf &) {}
  void operator()(const LifetimeParameter &) {}
  void operator()(const LifetimePredicate &) {}
  void operator()(const TypePredicate &p) { recurse_(*p.subject); }
  void operator()(const Parameter &p) { recurse_(*p.type); }
  void operator()(const StructField &p) { recurse_(*p.type); }
  void operator()(const FieldInit &p) { recurse_(*p.value); }
  void operator()(const Function &p) {
    generics(p.generics);
    if (p.signature.receiver)
      recurse_(*p.signature.receiver);
    for (const auto &parameter : p.signature.parameters)
      recurse_(parameter);
    if (p.signature.return_annotation)
      recurse_(**p.signature.return_annotation);
    where_clause(p.where_clause);
    recurse_(*p.body);
  }
  void operator()(const Struct &p) {
    generics(p.generics);
    where_clause(p.where_clause);
    for (const auto &field : p.fields)
      recurse_(field);
  }
  void operator()(const Constant &p) {
    recurse_(*p.type);
    recurse_(*p.value);
  }
  void operator()(const Impl &p) {
    generics(p.generics);
    recurse_(*p.target);
    where_clause(p.where_clause);
    for (const auto &member : p.members)
      dispatch(member, [&](const auto &owner, const auto &) { recurse_(owner); });
  }
  void operator()(const UnitType &) {}
  void operator()(const PathType &p) { path(p.path); }
  void operator()(const ReferenceType &p) { recurse_(*p.referent); }
  void operator()(const ArrayType &p) {
    recurse_(*p.element);
    recurse_(*p.length);
  }
  void operator()(const ParenType &p) { recurse_(*p.inner); }
  void operator()(const EmptyStmt &) {}
  void operator()(const LetStmt &p) {
    if (p.annotation)
      recurse_(**p.annotation);
    recurse_(*p.initializer);
  }
  void operator()(const ExprStmt &p) { recurse_(*p.expression); }
  void operator()(const IntegerExpr &) {}
  void operator()(const BooleanExpr &) {}
  void operator()(const UnitExpr &) {}
  void operator()(const ContinueExpr &) {}
  void operator()(const PathExpr &p) { path(p.path); }
  void operator()(const ParenExpr &p) { recurse_(*p.inner); }
  void operator()(const ArrayExpr &p) {
    for (const auto &element : p.elements)
      recurse_(*element);
  }
  void operator()(const RepeatArrayExpr &p) {
    recurse_(*p.element);
    recurse_(*p.count);
  }
  void operator()(const StructExpr &p) {
    path(p.path);
    for (const auto &field : p.fields)
      recurse_(field);
  }
  void operator()(const BlockExpr &p) { recurse_(*p.block); }
  void operator()(const IfExpr &p) {
    recurse_(*p.condition);
    recurse_(*p.then_block);
    if (p.else_branch)
      std::visit([&](const auto &branch) { recurse_(*branch); }, *p.else_branch);
  }
  void operator()(const LoopExpr &p) { recurse_(*p.body); }
  void operator()(const WhileExpr &p) {
    recurse_(*p.condition);
    recurse_(*p.body);
  }
  void operator()(const UnaryExpr &p) { recurse_(*p.operand); }
  void operator()(const BorrowExpr &p) { recurse_(*p.operand); }
  void operator()(const BinaryExpr &p) {
    recurse_(*p.left);
    recurse_(*p.right);
  }
  void operator()(const AssignExpr &p) {
    recurse_(*p.target);
    recurse_(*p.value);
  }
  void operator()(const CompoundAssignExpr &p) {
    recurse_(*p.target);
    recurse_(*p.value);
  }
  void operator()(const CastExpr &p) {
    recurse_(*p.value);
    recurse_(*p.target_type);
  }

private:
  void arguments(const std::optional<GenericArguments> &value) {
    if (value)
      for (const auto &argument : value->values)
        std::visit(Overloaded{[&](const TypePtr &p) { recurse_(*p); },
                              [&](const LifetimeName &) {}}, argument);
  }
  void path(const Path &value) {
    for (const auto &segment : value.segments)
      arguments(segment.arguments);
  }
  void generics(const std::optional<GenericParameters> &value) {
    if (value)
      for (const auto &parameter : value->lifetimes)
        recurse_(parameter);
  }
  void where_clause(const std::optional<WhereClause> &value) {
    if (value)
      for (const auto &predicate : value->predicates)
        dispatch(predicate, [&](const auto &owner, const auto &) { recurse_(owner); });
  }
  Recurse &recurse_;
};
} // namespace detail

// Enumerate immediate identity-bearing children; recursion belongs to the caller.
template <class T, class Recurse>
void for_each_child(const T &node, Recurse &&recurse) {
  detail::ChildEnumerator<std::remove_reference_t<Recurse>> children(recurse);
  dispatch(node, [&](const auto &, const auto &p) { children(p); });
}
} // namespace rx::ast
