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
} // namespace detail

// A catch-all overload is an explicit choice to accept future alternatives.
template <class V, class T>
concept ExhaustiveVisitor = std::derived_from<T, Node> &&
    detail::VisitorCoverage<V, T,
      std::remove_cvref_t<decltype(detail::payload(std::declval<const T &>()))>>::value;

template <class T, class V> requires ExhaustiveVisitor<V, T>
decltype(auto) dispatch(const T &node, V &&visitor) {
  const auto &value = detail::payload(node);
  if constexpr (requires { std::variant_size<std::remove_cvref_t<decltype(value)>>::value; }) {
    return std::visit([&](const auto &p) -> decltype(auto) {
      return std::invoke(visitor, node, p);
    }, value);
  } else {
    return std::invoke(visitor, node, value);
  }
}
} // namespace rx::ast
