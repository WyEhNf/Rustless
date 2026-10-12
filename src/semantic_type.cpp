#include "rx/semantic_type.hpp"

#include <map>
#include <stdexcept>
#include <tuple>
#include <unordered_set>

namespace rx {
SemanticType::Kind SemanticType::kind() const noexcept {
  using K = Kind;
  return std::visit(
      ast::Overloaded{
          [](const ErrorType &) { return K::Error; },
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
          [](const FunctionType &) { return K::Function; },
          [](const BuiltinFunctionType &) { return K::BuiltinFunction; }},
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
    BuiltinId builtin{BuiltinId::GetInt};
    bool operator<(const Key &other) const {
      const auto fields = std::tie(kind, symbol, length, mutability, builtin);
      const auto other_fields = std::tie(other.kind, other.symbol, other.length,
                                         other.mutability, other.builtin);
      if (fields != other_fields)
        return fields < other_fields;
      return std::less<const SemanticType *>{}(element, other.element);
    }
  };
  std::map<Key, SemanticTypePtr> types;
  std::map<SymbolId, std::pair<SemanticType::Kind, std::string>> declarations;
  std::unordered_set<const SemanticType *> owned;
};

TypeContext::TypeContext() : impl_(std::make_unique<Impl>()) {}
TypeContext::~TypeContext() = default;

SemanticTypePtr TypeContext::intern(SemanticType::Data data) {
  using K = SemanticType::Kind;
  SemanticType value(std::move(data));
  Impl::Key key{value.kind()};
  if (key.kind == K::Struct || key.kind == K::Function) {
    key.symbol = value.symbol();
    if (!key.symbol.value || value.name().empty())
      throw std::invalid_argument("nominal types need an identity and name");
    const auto declaration = std::pair{key.kind, value.name()};
    const auto found = impl_->declarations.find(key.symbol);
    if (found != impl_->declarations.end() && found->second != declaration)
      throw std::invalid_argument("declaration identity has conflicting types");
    impl_->declarations.emplace(key.symbol, declaration);
  }
  if (key.kind == K::Box || key.kind == K::Vec || key.kind == K::Array ||
      key.kind == K::Ref)
    key.element = value.element().get();
  if (key.kind == K::Array)
    key.length = value.length();
  if (key.kind == K::Ref)
    key.mutability = value.is_mutable() ? ast::Mutability::Mutable
                                        : ast::Mutability::Immutable;
  if (const auto *builtin = get_if<BuiltinFunctionType>(value)) {
    key.builtin = builtin->builtin;
    key.element = builtin->owner.get();
  }
  if (const auto found = impl_->types.find(key); found != impl_->types.end()) {
    if ((key.kind == K::Struct || key.kind == K::Function) &&
        found->second->name() != value.name())
      throw std::invalid_argument("declaration identity has conflicting names");
    return found->second;
  }
  SemanticTypePtr result(new const SemanticType(std::move(value.data_)));
  impl_->types.emplace(key, result);
  impl_->owned.insert(result.get());
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

SemanticTypePtr TypeContext::structure(SymbolId symbol, std::string name) {
  return intern(StructType{symbol, std::move(name)});
}
SemanticTypePtr TypeContext::function(SymbolId symbol, std::string name) {
  return intern(FunctionType{symbol, std::move(name)});
}
SemanticTypePtr TypeContext::builtin(BuiltinId builtin, SemanticTypePtr owner) {
  switch (builtin) {
  case BuiltinId::GetInt:
  case BuiltinId::PrintInt:
  case BuiltinId::PrintlnInt:
  case BuiltinId::BoxNew:
  case BuiltinId::VecNew:
  case BuiltinId::Clone:
  case BuiltinId::Len:
  case BuiltinId::IsEmpty:
  case BuiltinId::Push:
  case BuiltinId::Remove:
    break;
  default:
    throw std::invalid_argument("unknown builtin function identity");
  }
  return intern(BuiltinFunctionType{builtin, owner ? canonical(std::move(owner))
                                                   : SemanticTypePtr{}});
}
SemanticTypePtr TypeContext::box(SemanticTypePtr element) {
  return intern(BoxType{canonical(std::move(element))});
}
SemanticTypePtr TypeContext::vector(SemanticTypePtr element) {
  return intern(VecType{canonical(std::move(element))});
}
SemanticTypePtr TypeContext::array(SemanticTypePtr element,
                                   std::uint64_t length) {
  return intern(ArrayType{canonical(std::move(element)), length});
}
SemanticTypePtr TypeContext::reference(SemanticTypePtr element,
                                       ast::Mutability mutability) {
  if (mutability != ast::Mutability::Immutable &&
      mutability != ast::Mutability::Mutable)
    throw std::invalid_argument("invalid reference mutability");
  return intern(RefType{canonical(std::move(element)), mutability});
}

SemanticTypePtr TypeContext::canonical(SemanticTypePtr value) {
  if (!value)
    throw std::invalid_argument("semantic type elements cannot be null");
  if (impl_->owned.contains(value.get()))
    return value;
  using K = SemanticType::Kind;
  switch (value->kind()) {
  case K::Struct:
    return structure(value->symbol(), value->name());
  case K::Function:
    return function(value->symbol(), value->name());
  case K::BuiltinFunction: {
    const auto &payload = *get_if<BuiltinFunctionType>(*value);
    return builtin(payload.builtin, payload.owner);
  }
  case K::Box:
    return box(value->element());
  case K::Vec:
    return vector(value->element());
  case K::Array:
    return array(value->element(), value->length());
  case K::Ref:
    return reference(value->element(), get_if<RefType>(*value)->mutability);
  default:
    return primitive(value->kind());
  }
}

bool same_type(const SemanticTypePtr &left, const SemanticTypePtr &right) {
  if (!left || !right)
    return false;
  if (left == right)
    return true;
  if (left->kind() != right->kind())
    return false;
  return std::visit(
      ast::Overloaded{[](const ErrorType &) { return true; },
                      [](const NeverType &) { return true; },
                      [](const UnitType &) { return true; },
                      [](const BoolType &) { return true; },
                      [](const I32Type &) { return true; },
                      [](const U32Type &) { return true; },
                      [](const IsizeType &) { return true; },
                      [](const UsizeType &) { return true; },
                      [&](const StructType &p) {
                        return p.symbol == get_if<StructType>(*right)->symbol;
                      },
                      [&](const BoxType &p) {
                        return same_type(p.element, right->element());
                      },
                      [&](const VecType &p) {
                        return same_type(p.element, right->element());
                      },
                      [&](const ArrayType &p) {
                        return p.length == right->length() &&
                               same_type(p.element, right->element());
                      },
                      [&](const RefType &p) {
                        return p.mutability ==
                                   get_if<RefType>(*right)->mutability &&
                               same_type(p.element, right->element());
                      },
                      [&](const FunctionType &p) {
                        return p.symbol == get_if<FunctionType>(*right)->symbol;
                      },
                      [&](const BuiltinFunctionType &p) {
                        const auto &other =
                            *get_if<BuiltinFunctionType>(*right);
                        return p.builtin == other.builtin &&
                               ((!p.owner && !other.owner) ||
                                same_type(p.owner, other.owner));
                      }},
      left->data());
}

std::string type_name(const SemanticTypePtr &type) {
  if (!type)
    return "<missing>";
  return std::visit(
      ast::Overloaded{
          [](const ErrorType &) -> std::string { return "<error>"; },
          [](const NeverType &) -> std::string { return "!"; },
          [](const UnitType &) -> std::string { return "()"; },
          [](const BoolType &) -> std::string { return "bool"; },
          [](const I32Type &) -> std::string { return "i32"; },
          [](const U32Type &) -> std::string { return "u32"; },
          [](const IsizeType &) -> std::string { return "isize"; },
          [](const UsizeType &) -> std::string { return "usize"; },
          [](const StructType &p) { return p.name; },
          [](const BoxType &p) { return "Box<" + type_name(p.element) + ">"; },
          [](const VecType &p) { return "Vec<" + type_name(p.element) + ">"; },
          [](const ArrayType &p) {
            return "[" + type_name(p.element) + "; " +
                   std::to_string(p.length) + "]";
          },
          [](const RefType &p) {
            return std::string("&") +
                   (p.mutability == ast::Mutability::Mutable ? "mut " : "") +
                   type_name(p.element);
          },
          [](const FunctionType &p) { return "fn " + p.name; },
          [](const BuiltinFunctionType &p) {
            auto name = "fn " + std::string(builtin_name(p.builtin));
            if (p.owner)
              name += " (owner " + type_name(p.owner) + ")";
            return name;
          }},
      type->data());
}
} // namespace rx
