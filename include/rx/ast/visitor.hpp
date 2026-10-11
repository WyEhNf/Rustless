#pragma once

#include "rx/ast/item.hpp"
#include <type_traits>

namespace rx::ast {
// Preorder traversal. Each syntax identity is reported once, before its children.
template <class Visitor> class Walker {
public:
  explicit Walker(Visitor &visitor) : visitor_(visitor) {}
  void walk(const Crate &node) {
    visitor_(node);
    for (const auto &item : node.items) std::visit([&](const auto &p) { walk(p); }, item);
  }
  void walk(const Expr &node) {
    visitor_(node);
    visit(node, [&](const auto &p) { children(p); });
  }
  void walk(const TypeSyntax &node) {
    visitor_(node);
    visit(node, Overloaded{
      [&](const UnitType &) {},
      [&](const PathType &p) { path(p.path); },
      [&](const ReferenceType &p) { walk(*p.referent); },
      [&](const ArrayType &p) { walk(*p.element); walk(*p.length); },
      [&](const ParenType &p) { walk(*p.inner); }
    });
  }
  void walk(const Block &node) {
    visitor_(node);
    for (const auto &statement : node.statements) walk(*statement);
    if (node.tail) walk(**node.tail);
  }
  void walk(const Stmt &node) {
    visitor_(node);
    visit(node, Overloaded{
      [&](const EmptyStmt &) {},
      [&](const LetStmt &p) { if (p.annotation) walk(**p.annotation); walk(*p.initializer); },
      [&](const ExprStmt &p) { walk(*p.expression); }
    });
  }
  void walk(const Use &node) { visitor_(node); walk(node.tree); }
  void walk(const UseTree &node) {
    visitor_(node);
    std::visit(Overloaded{
      [&](const NameImport &) {}, [&](const GlobImport &) {},
      [&](const GroupImport &p) { for (const auto &child : p.children) walk(child); }
    }, node.target);
  }
  void walk(const Function &node) {
    visitor_(node); generics(node.generics);
    if (node.signature.receiver) visitor_(*node.signature.receiver);
    for (const auto &parameter : node.signature.parameters) {
      visitor_(parameter); walk(*parameter.type);
    }
    if (node.signature.return_annotation) walk(**node.signature.return_annotation);
    where_clause(node.where_clause); walk(*node.body);
  }
  void walk(const Struct &node) {
    visitor_(node); generics(node.generics); where_clause(node.where_clause);
    for (const auto &field : node.fields) { visitor_(field); walk(*field.type); }
  }
  void walk(const Constant &node) { visitor_(node); walk(*node.type); walk(*node.value); }
  void walk(const Impl &node) {
    visitor_(node); generics(node.generics); walk(*node.target); where_clause(node.where_clause);
    for (const auto &member : node.members) std::visit([&](const auto &p) { walk(p); }, member);
  }
  void walk(const IfBranch &node) { visitor_(node); children(node.data); }
private:
  void path(const Path &value) {
    for (const auto &segment : value.segments) arguments(segment.arguments);
  }
  void arguments(const std::optional<GenericArguments> &value) {
    if (value) for (const auto &argument : value->values) std::visit(Overloaded{
      [&](const TypePtr &p) { walk(*p); }, [&](const LifetimeName &) {}
    }, argument);
  }
  void generics(const std::optional<GenericParameters> &value) {
    if (value) for (const auto &parameter : value->lifetimes) visitor_(parameter);
  }
  void where_clause(const std::optional<WhereClause> &value) {
    if (value) for (const auto &predicate : value->predicates) std::visit(Overloaded{
      [&](const LifetimePredicate &p) { visitor_(p); },
      [&](const TypePredicate &p) { visitor_(p); walk(*p.subject); }
    }, predicate);
  }
  void children(const IntegerExpr &) {}
  void children(const BooleanExpr &) {}
  void children(const UnitExpr &) {}
  void children(const ContinueExpr &) {}
  void children(const PathExpr &p) { path(p.path); }
  void children(const ParenExpr &p) { walk(*p.inner); }
  void children(const ArrayExpr &p) { for (const auto &element : p.elements) walk(*element); }
  void children(const RepeatArrayExpr &p) { walk(*p.element); walk(*p.count); }
  void children(const StructExpr &p) {
    path(p.path);
    for (const auto &field : p.fields) { visitor_(field); walk(*field.value); }
  }
  void children(const BlockExpr &p) { walk(*p.block); }
  void children(const IfExpr &p) {
    walk(*p.condition); walk(*p.then_block);
    if (p.else_branch) std::visit([&](const auto &branch) { walk(*branch); }, *p.else_branch);
  }
  void children(const LoopExpr &p) { walk(*p.body); }
  void children(const WhileExpr &p) { walk(*p.condition); walk(*p.body); }
  void children(const UnaryExpr &p) { walk(*p.operand); }
  void children(const BorrowExpr &p) { walk(*p.operand); }
  void children(const BinaryExpr &p) { walk(*p.left); walk(*p.right); }
  void children(const AssignExpr &p) { walk(*p.target); walk(*p.value); }
  void children(const CompoundAssignExpr &p) { walk(*p.target); walk(*p.value); }
  void children(const CastExpr &p) { walk(*p.value); walk(*p.target_type); }
  void children(const CallExpr &p) { walk(*p.callee); for (const auto &argument : p.arguments) walk(*argument); }
  void children(const MethodCallExpr &p) {
    walk(*p.receiver); arguments(p.generic_args);
    for (const auto &argument : p.arguments) walk(*argument);
  }
  void children(const FieldExpr &p) { walk(*p.base); }
  void children(const IndexExpr &p) { walk(*p.base); walk(*p.index); }
  void children(const BreakExpr &p) { if (p.value) walk(**p.value); }
  void children(const ReturnExpr &p) { if (p.value) walk(**p.value); }
  Visitor &visitor_;
};

template <class Visitor> void walk(const Crate &crate, Visitor &&visitor) {
  Walker<std::remove_reference_t<Visitor>> walker(visitor);
  walker.walk(crate);
}
} // namespace rx::ast
