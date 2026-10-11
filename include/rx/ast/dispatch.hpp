#pragma once

#include "rx/ast/item.hpp"
#include <concepts>
#include <type_traits>

namespace rx::ast {
namespace detail {
// Owners provide identity; payloads provide the concrete syntax shape.
template <std::derived_from<Node> T> const T &payload(const T &node) {
  return node;
}
inline const auto &payload(const Expr &node) { return node.data; }
inline const auto &payload(const TypeSyntax &node) { return node.data; }
inline const auto &payload(const Stmt &node) { return node.data; }
inline const auto &payload(const SelfParam &node) { return node.data; }
inline const auto &payload(const IfBranch &node) { return node.data; }
inline const auto &payload(const UseTree &node) { return node.target; }

template <class V, class Owner, class P> struct VisitorCoverage {
  static constexpr bool value = std::is_invocable_v<V &, const Owner &, const P &>;
};
template <class V, class Owner, class... Ps>
struct VisitorCoverage<V, Owner, std::variant<Ps...>> {
  static consteval bool compatible() {
    if constexpr ((std::is_invocable_v<V &, const Owner &, const Ps &> && ...)) {
      using First = std::variant_alternative_t<0, std::variant<Ps...>>;
      using Result = std::invoke_result_t<V &, const Owner &, const First &>;
      return (std::same_as<Result,
                          std::invoke_result_t<V &, const Owner &, const Ps &>> && ...);
    }
    return false;
  }
  static constexpr bool value = compatible();
};
template <class V, class T> struct Coverage;
// Register identity-bearing owners once for dispatch and whole-tree coverage.
template <class... Owners> struct OwnerTypes {
  template <class T> static constexpr bool contains = (std::same_as<T, Owners> || ...);
  template <class V> static constexpr bool covered = (Coverage<V, Owners>::value && ...);
};
using SyntaxOwners = OwnerTypes<Crate, Expr, TypeSyntax, Stmt, Block, Use,
    UseTree, Function, Struct, Constant, Impl, IfBranch, SelfParam, Parameter,
    StructField, FieldInit, LifetimeParameter, LifetimePredicate, TypePredicate>;
template <class T> struct RootType : std::bool_constant<SyntaxOwners::contains<T>> {};
template <class... Ts> struct RootType<std::variant<Ts...>>
    : std::bool_constant<(SyntaxOwners::contains<Ts> && ...)> {};

template <class V, class T> struct Coverage : std::false_type {};
template <class V, class T> requires SyntaxOwners::contains<T>
struct Coverage<V, T> : VisitorCoverage<V, T,
    std::remove_cvref_t<decltype(payload(std::declval<const T &>()))>> {};
} // namespace detail

template <class T>
concept SyntaxRoot = detail::RootType<std::remove_cvref_t<T>>::value;

// A catch-all overload is an explicit choice to accept future alternatives.
template <class V, class T>
concept ExhaustiveVisitor = detail::Coverage<V, T>::value;

template <class T, class V>
  requires detail::SyntaxOwners::contains<T> && ExhaustiveVisitor<V, T>
decltype(auto) visit(const T &node, V &&visitor) {
  const auto &value = detail::payload(node);
  if constexpr (requires { std::variant_size<std::remove_cvref_t<decltype(value)>>::value; }) {
    return std::visit([&](const auto &p) -> decltype(auto) {
      return std::invoke(visitor, node, p);
    }, value);
  } else {
    return std::invoke(visitor, node, value);
  }
}

namespace detail {
template <class V, class... Ts>
struct Coverage<V, std::variant<Ts...>> {
  static consteval bool compatible() {
    if constexpr ((ExhaustiveVisitor<V, Ts> && ...)) {
      using First = std::variant_alternative_t<0, std::variant<Ts...>>;
      using Result = decltype(visit(std::declval<const First &>(),
                                       std::declval<V &>()));
      return (std::same_as<Result,
          decltype(visit(std::declval<const Ts &>(), std::declval<V &>()))> && ...);
    }
    return false;
  }
  static constexpr bool value = compatible();
};
} // namespace detail

// Item, ImplMember and WherePredicate select their own concrete owner.
template <class... Ts, class V>
  requires ExhaustiveVisitor<V, std::variant<Ts...>>
decltype(auto) visit(const std::variant<Ts...> &nodes, V &&visitor) {
  return std::visit([&](const auto &node) -> decltype(auto) {
    return visit(node, visitor);
  }, nodes);
}
} // namespace rx::ast
