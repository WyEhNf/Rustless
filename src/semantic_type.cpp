#include "rx/semantic_type.hpp"

#include <map>
#include <stdexcept>
#include <tuple>

namespace rx {
SemanticType::Kind SemanticType::kind() const noexcept {
  using K = Kind;
  return std::visit(
      ast::Overloaded{[](const ErrorType &) { return K::Error; },
                      [](const NeverType &) { return K::Never; },
                      [](const UnitType &) { return K::Unit; },
                      [](const BoolType &) { return K::Bool; },
                      [](const I32Type &) { return K::I32; },
                      [](const U32Type &) { return K::U32; },
                      [](const IsizeType &) { return K::Isize; },
                      [](const UsizeType &) { return K::Usize; },
                      [](const StructType &) { return K::Struct; },
                      [](const BoxType &) { return K::Box; },
                      [](const VecType &) { return K::Vec; },
                      [](const ArrayType &) { return K::Array; },
                      [](const RefType &) { return K::Ref; },
                      [](const FunctionType &) { return K::Function; }},
      data_);
}

const SemanticTypePtr &SemanticType::element() const {
  return std::visit(
      [](const auto &payload) -> const SemanticTypePtr & {
        if constexpr (requires { payload.element; })
          return payload.element;
        else
          throw std::logic_error("semantic type has no element");
      },
      data_);
}
std::uint64_t SemanticType::length() const {
  if (const auto *array = get_if<ArrayType>(*this))
    return array->length;
  throw std::logic_error("semantic type is not an array");
}
bool SemanticType::is_mutable() const {
  if (const auto *reference = get_if<RefType>(*this))
    return reference->mutability == ast::Mutability::Mutable;
  throw std::logic_error("semantic type is not a reference");
}
const std::string &SemanticType::name() const {
  if (const auto *structure = get_if<StructType>(*this))
    return structure->name;
  if (const auto *function = get_if<FunctionType>(*this))
    return function->name;
  throw std::logic_error("semantic type has no declaration name");
}
SymbolId SemanticType::symbol() const {
  if (const auto *structure = get_if<StructType>(*this))
    return structure->symbol;
  if (const auto *function = get_if<FunctionType>(*this))
    return function->symbol;
  throw std::logic_error("semantic type has no declaration identity");
}
} // namespace rx
