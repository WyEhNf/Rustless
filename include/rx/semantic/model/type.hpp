#pragma once

#include "rx/ast/names.hpp"
#include "rx/semantic/model/builtin.hpp"
#include "rx/semantic/model/symbol.hpp"
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <type_traits>
#include <variant>

namespace rx {
class SemanticType;
using SemanticTypePtr = std::shared_ptr<const SemanticType>;

struct ErrorType {};
struct NeverType {};
struct UnitType {};
struct BoolType {};
struct I32Type {};
struct U32Type {};
struct IsizeType {};
struct UsizeType {};
struct StructType {
  SymbolId symbol;
  std::string name;
};
struct BoxType {
  SemanticTypePtr element;
};
struct VecType {
  SemanticTypePtr element;
};
struct ArrayType {
  SemanticTypePtr element;
  std::uint64_t length;
};
struct RefType {
  SemanticTypePtr element;
  ast::Mutability mutability;
};
struct FunctionType {
  SymbolId symbol;
  std::string name;
};
struct BuiltinFunctionType {
  BuiltinId builtin;
  SemanticTypePtr owner;
};

class TypeContext;
class SemanticType final {
public:
  enum class Kind {
    Error,
    Never,
    Unit,
    Bool,
    I32,
    U32,
    Isize,
    Usize,
    Struct,
    Box,
    Vec,
    Array,
    Ref,
    Function,
    BuiltinFunction
  };
  using Data =
      std::variant<ErrorType, NeverType, UnitType, BoolType, I32Type, U32Type,
                   IsizeType, UsizeType, StructType, BoxType, VecType,
                   ArrayType, RefType, FunctionType, BuiltinFunctionType>;
  Kind kind() const noexcept;
  const Data &data() const noexcept { return data_; }
  const SemanticTypePtr &element() const;
  std::uint64_t length() const;
  bool is_mutable() const;
  const std::string &name() const;
  SymbolId symbol() const;

private:
  friend class TypeContext;
  explicit SemanticType(Data data) : data_(std::move(data)) {}
  Data data_;
};

class TypeContext final {
public:
  TypeContext();
  ~TypeContext();
  TypeContext(const TypeContext &) = delete;
  TypeContext &operator=(const TypeContext &) = delete;
  SemanticTypePtr primitive(SemanticType::Kind kind);
  SemanticTypePtr structure(SymbolId symbol, std::string name);
  SemanticTypePtr function(SymbolId symbol, std::string name);
  SemanticTypePtr builtin(BuiltinId builtin, SemanticTypePtr owner = {});
  SemanticTypePtr box(SemanticTypePtr element);
  SemanticTypePtr vector(SemanticTypePtr element);
  SemanticTypePtr array(SemanticTypePtr element, std::uint64_t length);
  SemanticTypePtr reference(SemanticTypePtr element,
                            ast::Mutability mutability);

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
  SemanticTypePtr intern(SemanticType::Data data);
  SemanticTypePtr canonical(SemanticTypePtr value);
};

bool same_type(const SemanticTypePtr &left, const SemanticTypePtr &right);
std::string type_name(const SemanticTypePtr &type);

template <class Payload> const Payload *get_if(const SemanticType &type) {
  return std::get_if<Payload>(&type.data());
}

namespace detail {
template <class Visitor, class Data> struct SemanticCoverage;
template <class Visitor, class... Payloads>
struct SemanticCoverage<Visitor, std::variant<Payloads...>> {
  static constexpr bool compatible() {
    constexpr bool accepts_all_payloads =
        (std::is_invocable_v<Visitor &, const SemanticType &,
                             const Payloads &> &&
         ...);
    if constexpr (accepts_all_payloads) {
      using First = std::variant_alternative_t<0, std::variant<Payloads...>>;
      using Result =
          std::invoke_result_t<Visitor &, const SemanticType &, const First &>;
      constexpr bool returns_same_type =
          (std::is_same_v<Result,
                          std::invoke_result_t<Visitor &, const SemanticType &,
                                               const Payloads &>> &&
           ...);
      return returns_same_type;
    }
    return false;
  }
};
} // namespace detail

template <class Visitor>
inline constexpr bool SemanticTypeVisitor =
    detail::SemanticCoverage<Visitor, SemanticType::Data>::compatible();

template <class Visitor,
          std::enable_if_t<SemanticTypeVisitor<Visitor>, int> = 0>
decltype(auto) visit(const SemanticType &type, Visitor &&visitor) {
  return std::visit(
      [&](const auto &payload) -> decltype(auto) {
        return std::invoke(visitor, type, payload);
      },
      type.data());
}
} // namespace rx
