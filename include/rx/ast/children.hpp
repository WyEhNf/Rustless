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
