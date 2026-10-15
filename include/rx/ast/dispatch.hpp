#pragma once

#include "rx/ast/item.hpp"
#include <functional>
#include <type_traits>
#include <variant>

namespace rx::ast {
namespace detail {
template <class Owner> const Owner &payload(const Owner &node) { return node; }
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
          std::is_same<Result, std::invoke_result_t<Visitor &, const Owner &,
                                                    const Payloads &>>...>;
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
    : std::conjunction<std::bool_constant<SyntaxOwners::contains<Owners>>...> {
};

template <class Visitor, class Owner, class Enable>
struct Coverage : std::false_type {};
template <class Visitor, class Owner>
struct Coverage<Visitor, Owner, std::enable_if_t<SyntaxOwners::contains<Owner>>>
    : VisitorCoverage<
          Visitor, Owner,
          std::decay_t<decltype(payload(std::declval<const Owner &>()))>> {};

template <class Owner, class Payload, class Visitor>
decltype(auto) dispatch_payload(const Owner &node, const Payload &value,
                                Visitor &visitor) {
  return std::invoke(visitor, node, value);
}

template <class Owner, class... Payloads, class Visitor>
decltype(auto) dispatch_payload(const Owner &node,
                                const std::variant<Payloads...> &value,
                                Visitor &visitor) {
  return std::visit(
      [&](const auto &data) -> decltype(auto) {
        return std::invoke(visitor, node, data);
      },
      value);
}
}

template <class Owner>
inline constexpr bool SyntaxRoot = detail::RootType<std::decay_t<Owner>>::value;

template <class Visitor, class Owner>
inline constexpr bool ExhaustiveVisitor =
    detail::Coverage<Visitor, Owner>::value;

template <class Owner, class Visitor,
          std::enable_if_t<detail::SyntaxOwners::contains<Owner> &&
                               ExhaustiveVisitor<Visitor, Owner>,
                           int> = 0>
decltype(auto) visit(const Owner &node, Visitor &&visitor) {
  const auto &value = detail::payload(node);
  return detail::dispatch_payload(node, value, visitor);
}

namespace detail {
template <class Visitor, class... Owners>
struct Coverage<Visitor, std::variant<Owners...>, void> {
  static constexpr bool compatible() {
    constexpr bool all_covered =
        std::conjunction_v<Coverage<Visitor, Owners>...>;
    if constexpr (all_covered) {
      using FirstOwner = std::variant_alternative_t<0, std::variant<Owners...>>;
      using Result = decltype(rx::ast::visit(std::declval<const FirstOwner &>(),
                                    std::declval<Visitor &>()));
      return std::conjunction_v<
          std::is_same<Result, decltype(rx::ast::visit(std::declval<const Owners &>(),
                                              std::declval<Visitor &>()))>...>;
    }
    return false;
  }
  static constexpr bool value = compatible();
};
}

template <class... Owners, class Visitor,
          std::enable_if_t<ExhaustiveVisitor<Visitor, std::variant<Owners...>>,
                           int> = 0>
decltype(auto) visit(const std::variant<Owners...> &nodes, Visitor &&visitor) {
  return std::visit(
      [&](const auto &node) -> decltype(auto) { return rx::ast::visit(node, visitor); },
      nodes);
}
}
