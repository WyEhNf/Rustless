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

struct TypeContext::Impl {
  struct Key {
    SemanticType::Kind kind;
    SymbolId symbol{};
    const SemanticType *element{};
    std::uint64_t length{};
    ast::Mutability mutability{ast::Mutability::Immutable};
    bool operator<(const Key &other) const {
      const auto fields = std::tie(kind, symbol, length, mutability);
      const auto other_fields =
          std::tie(other.kind, other.symbol, other.length, other.mutability);
      if (fields != other_fields)
        return fields < other_fields;
      return std::less<const SemanticType *>{}(element, other.element);
    }
  };
  std::map<Key, SemanticTypePtr> types;
};

TypeContext::TypeContext() : impl_(std::make_unique<Impl>()) {}
TypeContext::~TypeContext() = default;

SemanticTypePtr TypeContext::intern(SemanticType::Data data) {
  using K = SemanticType::Kind;
  SemanticType value(std::move(data));
  Impl::Key key{value.kind()};
  if (key.kind == K::Struct || key.kind == K::Function)
    key.symbol = value.symbol();
  if (key.kind == K::Box || key.kind == K::Vec || key.kind == K::Array ||
      key.kind == K::Ref)
    key.element = value.element().get();
  if (key.kind == K::Array)
    key.length = value.length();
  if (key.kind == K::Ref)
    key.mutability = value.is_mutable() ? ast::Mutability::Mutable
                                        : ast::Mutability::Immutable;
  if (const auto found = impl_->types.find(key); found != impl_->types.end()) {
    if ((key.kind == K::Struct || key.kind == K::Function) &&
        found->second->name() != value.name())
      throw std::invalid_argument("declaration identity has conflicting names");
    return found->second;
  }
  SemanticTypePtr result(new const SemanticType(std::move(value.data_)));
  impl_->types.emplace(key, result);
  return result;
}

SemanticTypePtr TypeContext::primitive(SemanticType::Kind kind) {
  using K = SemanticType::Kind;
  switch (kind) {
  case K::Error:
    return intern(ErrorType{});
  case K::Never:
    return intern(NeverType{});
  case K::Unit:
    return intern(UnitType{});
  case K::Bool:
    return intern(BoolType{});
  case K::I32:
    return intern(I32Type{});
  case K::U32:
    return intern(U32Type{});
  case K::Isize:
    return intern(IsizeType{});
  case K::Usize:
    return intern(UsizeType{});
  default:
    throw std::invalid_argument("primitive factory needs an atomic type");
  }
}
} // namespace rx
