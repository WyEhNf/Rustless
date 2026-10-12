#pragma once

#include "rx/ast/dispatch.hpp"

namespace rx::ast {
namespace detail {

template <class Callback> class ChildEnumerator {
public:
  explicit ChildEnumerator(Callback &callback) : visit_child_(callback) {}
  void operator()(const Crate &payload) {
    for (const auto &item : payload.items)
      visit(item,
            [&](const auto &owner, const auto &) { visit_child_(owner); });
  }
  void operator()(const Block &payload) {
    for (const auto &statement : payload.statements)
      visit_child_(*statement);
    if (payload.tail)
      visit_child_(**payload.tail);
  }
  void operator()(const Use &payload) { visit_child_(payload.tree); }
  void operator()(const NameImport &) {}
  void operator()(const GlobImport &) {}
  void operator()(const GroupImport &payload) {
    for (const auto &child : payload.children)
      visit_child_(child);
  }
  void operator()(const ValueSelf &) {}
  void operator()(const ReferenceSelf &) {}
  void operator()(const LifetimeParameter &) {}
  void operator()(const LifetimePredicate &) {}
  void operator()(const TypePredicate &payload) {
    visit_child_(*payload.subject);
  }
  void operator()(const Parameter &payload) { visit_child_(*payload.type); }
  void operator()(const StructField &payload) { visit_child_(*payload.type); }
  void operator()(const FieldInit &payload) { visit_child_(*payload.value); }
  void operator()(const Function &payload) {
    generics(payload.generics);
    if (payload.signature.receiver)
      visit_child_(*payload.signature.receiver);
    for (const auto &parameter : payload.signature.parameters)
      visit_child_(parameter);
    if (payload.signature.return_annotation)
      visit_child_(**payload.signature.return_annotation);
    where_clause(payload.where_clause);
    visit_child_(*payload.body);
  }
  void operator()(const Struct &payload) {
    generics(payload.generics);
    where_clause(payload.where_clause);
    for (const auto &field : payload.fields)
      visit_child_(field);
  }
  void operator()(const Constant &payload) {
    visit_child_(*payload.type);
    visit_child_(*payload.value);
  }
  void operator()(const Impl &payload) {
    generics(payload.generics);
    visit_child_(*payload.target);
    where_clause(payload.where_clause);
    for (const auto &member : payload.members)
      visit(member,
            [&](const auto &owner, const auto &) { visit_child_(owner); });
  }
  void operator()(const UnitType &) {}
  void operator()(const PathType &payload) { path(payload.path); }
  void operator()(const ReferenceType &payload) {
    visit_child_(*payload.referent);
  }
  void operator()(const ArrayType &payload) {
    visit_child_(*payload.element);
    visit_child_(*payload.length);
  }
  void operator()(const ParenType &payload) { visit_child_(*payload.inner); }
  void operator()(const EmptyStmt &) {}
  void operator()(const LetStmt &payload) {
    if (payload.annotation)
      visit_child_(**payload.annotation);
    visit_child_(*payload.initializer);
  }
  void operator()(const ExprStmt &payload) {
    visit_child_(*payload.expression);
  }
  void operator()(const IntegerExpr &) {}
  void operator()(const BooleanExpr &) {}
  void operator()(const UnitExpr &) {}
  void operator()(const ContinueExpr &) {}
  void operator()(const PathExpr &payload) { path(payload.path); }
  void operator()(const ParenExpr &payload) { visit_child_(*payload.inner); }
  void operator()(const ArrayExpr &payload) {
    for (const auto &element : payload.elements)
      visit_child_(*element);
  }
  void operator()(const RepeatArrayExpr &payload) {
    visit_child_(*payload.element);
    visit_child_(*payload.count);
  }
  void operator()(const StructExpr &payload) {
    path(payload.path);
    for (const auto &field : payload.fields)
      visit_child_(field);
  }
  void operator()(const BlockExpr &payload) { visit_child_(*payload.block); }
  void operator()(const IfExpr &payload) {
    visit_child_(*payload.condition);
    visit_child_(*payload.then_block);
    if (payload.else_branch)
      std::visit([&](const auto &branch) { visit_child_(*branch); },
                 *payload.else_branch);
  }
  void operator()(const LoopExpr &payload) { visit_child_(*payload.body); }
  void operator()(const WhileExpr &payload) {
    visit_child_(*payload.condition);
    visit_child_(*payload.body);
  }
  void operator()(const UnaryExpr &payload) { visit_child_(*payload.operand); }
  void operator()(const BorrowExpr &payload) { visit_child_(*payload.operand); }
  void operator()(const BinaryExpr &payload) {
    visit_child_(*payload.left);
    visit_child_(*payload.right);
  }
  void operator()(const AssignExpr &payload) {
    visit_child_(*payload.target);
    visit_child_(*payload.value);
  }
  void operator()(const CompoundAssignExpr &payload) {
    visit_child_(*payload.target);
    visit_child_(*payload.value);
  }
  void operator()(const CastExpr &payload) {
    visit_child_(*payload.value);
    visit_child_(*payload.target_type);
  }
  void operator()(const CallExpr &payload) {
    visit_child_(*payload.callee);
    for (const auto &argument : payload.arguments)
      visit_child_(*argument);
  }
  void operator()(const MethodCallExpr &payload) {
    visit_child_(*payload.receiver);
    arguments(payload.generic_args);
    for (const auto &argument : payload.arguments)
      visit_child_(*argument);
  }
  void operator()(const FieldExpr &payload) { visit_child_(*payload.base); }
  void operator()(const IndexExpr &payload) {
    visit_child_(*payload.base);
    visit_child_(*payload.index);
  }
  void operator()(const BreakExpr &payload) {
    if (payload.value)
      visit_child_(**payload.value);
  }
  void operator()(const ReturnExpr &payload) {
    if (payload.value)
      visit_child_(**payload.value);
  }

private:
  void arguments(const std::optional<GenericArguments> &value) {
    if (value)
      for (const auto &argument : value->values)
        std::visit(
            Overloaded{[&](const TypePtr &payload) { visit_child_(*payload); },
                       [&](const LifetimeName &) {}},
            argument);
  }
  void path(const Path &value) {
    for (const auto &segment : value.segments)
      arguments(segment.arguments);
  }
  void generics(const std::optional<GenericParameters> &value) {
    if (value)
      for (const auto &parameter : value->lifetimes)
        visit_child_(parameter);
  }
  void where_clause(const std::optional<WhereClause> &value) {
    if (value)
      for (const auto &predicate : value->predicates)
        visit(predicate,
              [&](const auto &owner, const auto &) { visit_child_(owner); });
  }
  Callback &visit_child_;
};
}

template <class Owner, class Callback,
          std::enable_if_t<SyntaxRoot<Owner>, int> = 0>
void for_each_child(const Owner &node, Callback &&callback) {
  detail::ChildEnumerator<std::remove_reference_t<Callback>> children(callback);
  visit(node, [&](const auto &, const auto &payload) { children(payload); });
}
}
