#pragma once

#include "rx/ast/item.hpp"
#include <functional>
#include <type_traits>
#include <variant>

namespace rx::ast {
namespace detail {
template <class Owner> const Owner &payload(const Owner &node) {
  return node;
}
inline const auto &payload(const Expr &node) { return node.data; }
inline const auto &payload(const TypeSyntax &node) { return node.data; }
inline const auto &payload(const Stmt &node) { return node.data; }
inline const auto &payload(const SelfParam &node) { return node.data; }
inline const auto &payload(const IfBranch &node) { return node.data; }
inline const auto &payload(const UseTree &node) { return node.target; }

template <class Visitor, class Owner, class Payload> struct VisitorCoverage {
  static constexpr bool value =
      std::is_invocable_v<Visitor &, const Owner &, const Payload &>;
};
template <class Visitor, class Owner, class... Payloads>
struct VisitorCoverage<Visitor, Owner, std::variant<Payloads...>> {
  static constexpr bool compatible() {
    constexpr bool all_callable = std::conjunction_v<
        std::is_invocable<Visitor &, const Owner &, const Payloads &>...>;
    if constexpr (all_callable) {
      using FirstPayload =
          std::variant_alternative_t<0, std::variant<Payloads...>>;
      using Result =
          std::invoke_result_t<Visitor &, const Owner &, const FirstPayload &>;
      return std::conjunction_v<
          std::is_same<Result, std::invoke_result_t<
                                   Visitor &, const Owner &, const Payloads &>>...>;
    }
    return false;
  }
  static constexpr bool value = compatible();
};
template <class Visitor, class Owner, class Enable = void> struct Coverage;
template <class... Owners> struct OwnerTypes {
  template <class Candidate>
  static constexpr bool contains =
      std::disjunction_v<std::is_same<Candidate, Owners>...>;
  template <class Visitor>
  static constexpr bool covered =
      std::conjunction_v<Coverage<Visitor, Owners>...>;
};
using SyntaxOwners =
    OwnerTypes<Crate, Expr, TypeSyntax, Stmt, Block, Use, UseTree, Function,
               Struct, Constant, Impl, IfBranch, SelfParam, Parameter,
               StructField, FieldInit, LifetimeParameter, LifetimePredicate,
               TypePredicate>;
template <class Owner>
struct RootType : std::bool_constant<SyntaxOwners::contains<Owner>> {};
template <class... Owners>
struct RootType<std::variant<Owners...>>
    : std::conjunction<std::bool_constant<SyntaxOwners::contains<Owners>>...> {};

template <class Visitor, class Owner, class Enable>
struct Coverage : std::false_type {};
template <class Visitor, class Owner>
struct Coverage<Visitor, Owner,
                std::enable_if_t<SyntaxOwners::contains<Owner>>>
    : VisitorCoverage<Visitor, Owner,
                      std::decay_t<decltype(payload(
                          std::declval<const Owner &>()))>> {};
}

template <class Owner>
inline constexpr bool SyntaxRoot = detail::RootType<std::decay_t<Owner>>::value;

template <class Visitor, class Owner>
inline constexpr bool ExhaustiveVisitor = detail::Coverage<Visitor, Owner>::value;

template <class T, class V>
  requires detail::SyntaxOwners::contains<T> && ExhaustiveVisitor<V, T>
decltype(auto) visit(const T &node, V &&visitor) {
  const auto &value = detail::payload(node);
  if constexpr (requires {
                  std::variant_size<
                      std::remove_cvref_t<decltype(value)>>::value;
                }) {
    return std::visit(
        [&](const auto &p) -> decltype(auto) {
          return std::invoke(visitor, node, p);
        },
        value);
  } else {
    return std::invoke(visitor, node, value);
  }
}

namespace detail {
template <class V, class... Ts> struct Coverage<V, std::variant<Ts...>> {
  static consteval bool compatible() {
    if constexpr ((ExhaustiveVisitor<V, Ts> && ...)) {
      using First = std::variant_alternative_t<0, std::variant<Ts...>>;
      using Result =
          decltype(visit(std::declval<const First &>(), std::declval<V &>()));
      return (std::same_as<Result, decltype(visit(std::declval<const Ts &>(),
                                                  std::declval<V &>()))> &&
              ...);
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
  return std::visit(
      [&](const auto &node) -> decltype(auto) { return visit(node, visitor); },
      nodes);
}
} // namespace rx::ast
