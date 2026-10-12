#pragma once

#include "rx/ir.hpp"
#include <functional>
#include <type_traits>

namespace rx::ir {
namespace detail {
template <class Visitor, class Owner, class Data> struct Coverage;

template <class Visitor, class Owner, class... Payloads>
struct Coverage<Visitor, Owner, std::variant<Payloads...>> {
  template <class Payload>
  using PayloadResult = std::invoke_result_t<Visitor &, const Owner &, const Payload &>;

  static constexpr bool complete() {
    constexpr bool accepts_all_payloads =
        (std::is_invocable_v<Visitor &, const Owner &, const Payloads &> && ...);
    if constexpr (accepts_all_payloads) {
      using FirstPayload = std::variant_alternative_t<0, std::variant<Payloads...>>;
      using ReturnType = PayloadResult<FirstPayload>;
      return (std::is_same_v<ReturnType, PayloadResult<Payloads>> && ...);
    }
    return false;
  }
  static constexpr bool value = complete();
};

template <class Visitor, class Owner> struct OwnerCoverage : std::false_type {};

template <class Visitor>
struct OwnerCoverage<Visitor, Instruction> : Coverage<Visitor, Instruction, Instruction::Data> {};

template <class Visitor>
struct OwnerCoverage<Visitor, Operand> : Coverage<Visitor, Operand, Operand::Data> {};
}

template <class Visitor, class Owner>
inline constexpr bool ExhaustiveVisitor = detail::OwnerCoverage<Visitor, Owner>::value;

template <class Owner, class Visitor> decltype(auto) visit(const Owner &node, Visitor &&visitor) {
  static_assert(ExhaustiveVisitor<Visitor, Owner>,
                "IR visitor must handle every payload with one return type");
  return std::visit(
      [&](const auto &payload) -> decltype(auto) { return std::invoke(visitor, node, payload); },
      node.data);
}

// Each required operand is enumerated through its named payload field.
template <class V> void for_each_operand(const Instruction &node, V &&visitor) {
  visit(node,
        ast::Overloaded{
            [&](const Instruction &, const Alloca &) {},
            [&](const Instruction &, const Load &p) { visitor(p.address); },
            [&](const Instruction &, const Store &p) {
              visitor(p.value);
              visitor(p.address);
            },
            [&](const Instruction &, const Gep &p) {
              visitor(p.base);
              visitor(p.offset);
            },
            [&](const Instruction &, const Binary &p) {
              visitor(p.left);
              visitor(p.right);
            },
            [&](const Instruction &, const Compare &p) {
              visitor(p.left);
              visitor(p.right);
            },
            [&](const Instruction &, const Cast &p) { visitor(p.operand); },
            [&](const Instruction &, const Call &p) {
              for (const auto &a : p.arguments)
                visitor(a);
            },
            [&](const Instruction &, const Phi &p) {
              for (const auto &a : p.incoming)
                visitor(a.value);
            },
            [&](const Instruction &, const Branch &) {},
            [&](const Instruction &, const CondBranch &p) {
              visitor(p.condition);
            },
            [&](const Instruction &, const Return &p) {
              if (p.value)
                visitor(*p.value);
            },
            [&](const Instruction &, const Unreachable &) {}});
}
template <class V>
void for_each_successor(const Instruction &node, V &&visitor) {
  visit(node,
        ast::Overloaded{
            [&](const Instruction &, const Alloca &) {},
            [&](const Instruction &, const Load &) {},
            [&](const Instruction &, const Store &) {},
            [&](const Instruction &, const Gep &) {},
            [&](const Instruction &, const Binary &) {},
            [&](const Instruction &, const Compare &) {},
            [&](const Instruction &, const Cast &) {},
            [&](const Instruction &, const Call &) {},
            [&](const Instruction &, const Phi &) {},
            [&](const Instruction &, const Branch &p) { visitor(p.target); },
            [&](const Instruction &, const CondBranch &p) {
              visitor(p.consequent);
              visitor(p.alternative);
            },
            [&](const Instruction &, const Return &) {},
            [&](const Instruction &, const Unreachable &) {}});
}
} // namespace rx::ir
