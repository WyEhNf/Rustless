#pragma once

#include "rx/ir.hpp"
#include <concepts>

namespace rx::ir {
namespace detail {
template <class V, class Owner, class Data> struct Coverage;
template <class V, class Owner, class... Ps>
struct Coverage<V, Owner, std::variant<Ps...>> {
  static consteval bool complete() {
    if constexpr ((std::invocable<V &, const Owner &, const Ps &> && ...)) {
      using First = std::variant_alternative_t<0, std::variant<Ps...>>;
      using R = std::invoke_result_t<V &, const Owner &, const First &>;
      return (std::same_as<R, std::invoke_result_t<V &, const Owner &, const Ps &>> && ...);
    }
    return false;
  }
  static constexpr bool value = complete();
};
} // namespace detail
template <class V, class T>
concept ExhaustiveVisitor = (std::same_as<T, Instruction> || std::same_as<T, Operand>) &&
    detail::Coverage<V, T, typename T::Data>::value;

template <class T, class V> requires ExhaustiveVisitor<V, T>
decltype(auto) visit(const T &node, V &&visitor) {
  return std::visit([&](const auto &p) -> decltype(auto) {
    return std::invoke(visitor, node, p);
  }, node.data);
}

// Each required operand is enumerated through its named payload field.
template <class V> void for_each_operand(const Instruction &node, V &&visitor) {
  visit(node, ast::Overloaded{
    [&](const Instruction &, const Alloca &) {},
    [&](const Instruction &, const Load &p) { visitor(p.address); },
    [&](const Instruction &, const Store &p) { visitor(p.value); visitor(p.address); },
    [&](const Instruction &, const Gep &p) { visitor(p.base); visitor(p.offset); },
    [&](const Instruction &, const Binary &p) { visitor(p.left); visitor(p.right); },
    [&](const Instruction &, const Compare &p) { visitor(p.left); visitor(p.right); },
    [&](const Instruction &, const Cast &p) { visitor(p.operand); },
    [&](const Instruction &, const Call &p) { for (const auto &a : p.arguments) visitor(a); },
    [&](const Instruction &, const Phi &p) { for (const auto &a : p.incoming) visitor(a.value); },
    [&](const Instruction &, const Branch &) {},
    [&](const Instruction &, const CondBranch &p) { visitor(p.condition); },
    [&](const Instruction &, const Return &p) { if (p.value) visitor(*p.value); },
    [&](const Instruction &, const Unreachable &) {}});
}
} // namespace rx::ir
