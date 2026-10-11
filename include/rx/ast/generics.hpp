#pragma once

#include "rx/ast/path.hpp"

namespace rx::ast {
struct LifetimeParameter : Node {
  LifetimeName name;
  std::vector<LifetimeName> bounds;
};
struct GenericParameters {
  Span span;
  std::vector<LifetimeParameter> lifetimes;
};
struct LifetimePredicate : Node {
  LifetimeName subject;
  std::vector<LifetimeName> bounds;
};
struct TypePredicate : Node {
  TypePtr subject;
  std::vector<LifetimeName> bounds;
};
using WherePredicate = std::variant<LifetimePredicate, TypePredicate>;
struct WhereClause {
  Span span;
  std::vector<WherePredicate> predicates;
};
} // namespace rx::ast
