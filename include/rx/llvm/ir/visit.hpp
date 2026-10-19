#pragma once

#include "rx/llvm/ir/ir.hpp"
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
      [&](const auto &payload) -> decltype(auto) {
        return std::invoke(visitor, node, payload);
      },
      node.data);
}

template <class Visitor> void for_each_operand(const Instruction &node, Visitor &&visitor) {
  auto enumerate = ast::Overloaded{
      [&](const Instruction &, const Alloca &) {},
      [&](const Instruction &, const Load &payload) {
        visitor(payload.address);
      },
      [&](const Instruction &, const Store &payload) {
        visitor(payload.value);
        visitor(payload.address);
      },
      [&](const Instruction &, const Gep &payload) {
        visitor(payload.base);
        visitor(payload.offset);
      },
      [&](const Instruction &, const Binary &payload) {
        visitor(payload.left);
        visitor(payload.right);
      },
      [&](const Instruction &, const Compare &payload) {
        visitor(payload.left);
        visitor(payload.right);
      },
      [&](const Instruction &, const Cast &payload) {
        visitor(payload.operand);
      },
      [&](const Instruction &, const Call &payload) {
        for (const auto &argument : payload.arguments) {
          visitor(argument);
        }
      },
      [&](const Instruction &, const Phi &payload) {
        for (const auto &argument : payload.incoming) {
          visitor(argument.value);
        }
      },
      [&](const Instruction &, const Branch &) {},
      [&](const Instruction &, const CondBranch &payload) {
        visitor(payload.condition);
      },
      [&](const Instruction &, const Return &payload) {
        if (payload.value) {
          visitor(*payload.value);
        }
      },
      [&](const Instruction &, const Unreachable &) {},
  };
  visit(node, enumerate);
}
template <class Visitor> void for_each_successor(const Instruction &node, Visitor &&visitor) {
  auto enumerate = ast::Overloaded{
      [&](const Instruction &, const Alloca &) {},
      [&](const Instruction &, const Load &) {},
      [&](const Instruction &, const Store &) {},
      [&](const Instruction &, const Gep &) {},
      [&](const Instruction &, const Binary &) {},
      [&](const Instruction &, const Compare &) {},
      [&](const Instruction &, const Cast &) {},
      [&](const Instruction &, const Call &) {},
      [&](const Instruction &, const Phi &) {},
      [&](const Instruction &, const Branch &payload) {
        visitor(payload.target);
      },
      [&](const Instruction &, const CondBranch &payload) {
        visitor(payload.consequent);
        visitor(payload.alternative);
      },
      [&](const Instruction &, const Return &) {},
      [&](const Instruction &, const Unreachable &) {},
  };
  visit(node, enumerate);
}
}
