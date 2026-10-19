#include "rx/semantic/model/type.hpp"

#include <map>
#include <stdexcept>
#include <tuple>
#include <unordered_set>

namespace rx {
SemanticType::Kind SemanticType::kind() const noexcept {
  using TypeKind = Kind;
  return std::visit(
      ast::Overloaded{[](const ErrorType &) { return TypeKind::Error; },
                      [](const NeverType &) { return TypeKind::Never; },
                      [](const UnitType &) { return TypeKind::Unit; },
                      [](const BoolType &) { return TypeKind::Bool; },
                      [](const I32Type &) { return TypeKind::I32; },
                      [](const U32Type &) { return TypeKind::U32; },
                      [](const IsizeType &) { return TypeKind::Isize; },
                      [](const UsizeType &) { return TypeKind::Usize; },
                      [](const StructType &) { return TypeKind::Struct; },
                      [](const BoxType &) { return TypeKind::Box; },
                      [](const VecType &) { return TypeKind::Vec; },
                      [](const ArrayType &) { return TypeKind::Array; },
                      [](const RefType &) { return TypeKind::Ref; },
                      [](const FunctionType &) { return TypeKind::Function; },
                      [](const BuiltinFunctionType &) {
                        return TypeKind::BuiltinFunction;
                      }},
      data_);
}

const SemanticTypePtr &SemanticType::element() const {
  if (const auto *box = get_if<BoxType>(*this))
    return box->element;
  if (const auto *vector = get_if<VecType>(*this))
    return vector->element;
  if (const auto *array = get_if<ArrayType>(*this))
    return array->element;
  if (const auto *reference = get_if<RefType>(*this))
    return reference->element;
  throw std::logic_error("semantic type has no element");
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
  using TypeKind = SemanticType::Kind;
  SemanticType value(std::move(data));
  Impl::Key key{value.kind()};
  if (key.kind == TypeKind::Struct || key.kind == TypeKind::Function) {
    key.symbol = value.symbol();
    if (!key.symbol.value || value.name().empty())
      throw std::invalid_argument("nominal types need an identity and name");
    const auto declaration = std::pair{key.kind, value.name()};
    const auto found = impl_->declarations.find(key.symbol);
    if (found != impl_->declarations.end() && found->second != declaration)
      throw std::invalid_argument("declaration identity has conflicting types");
    impl_->declarations.emplace(key.symbol, declaration);
  }
  if (key.kind == TypeKind::Box || key.kind == TypeKind::Vec ||
      key.kind == TypeKind::Array || key.kind == TypeKind::Ref)
    key.element = value.element().get();
  if (key.kind == TypeKind::Array)
    key.length = value.length();
  if (key.kind == TypeKind::Ref && value.is_mutable())
    key.mutability = ast::Mutability::Mutable;
  if (const auto *builtin = get_if<BuiltinFunctionType>(value)) {
    key.builtin = builtin->builtin;
    key.element = builtin->owner.get();
  }
  if (const auto found = impl_->types.find(key); found != impl_->types.end()) {
    if ((key.kind == TypeKind::Struct || key.kind == TypeKind::Function) &&
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
  using TypeKind = SemanticType::Kind;
  switch (kind) {
  case TypeKind::Error:
    return intern(ErrorType{});
  case TypeKind::Never:
    return intern(NeverType{});
  case TypeKind::Unit:
    return intern(UnitType{});
  case TypeKind::Bool:
    return intern(BoolType{});
  case TypeKind::I32:
    return intern(I32Type{});
  case TypeKind::U32:
    return intern(U32Type{});
  case TypeKind::Isize:
    return intern(IsizeType{});
  case TypeKind::Usize:
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
  if (owner)
    owner = canonical(std::move(owner));
  return intern(BuiltinFunctionType{builtin, std::move(owner)});
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
  if (impl_->owned.find(value.get()) != impl_->owned.end())
    return value;
  using TypeKind = SemanticType::Kind;
  switch (value->kind()) {
  case TypeKind::Struct:
    return structure(value->symbol(), value->name());
  case TypeKind::Function:
    return function(value->symbol(), value->name());
  case TypeKind::BuiltinFunction: {
    const auto &payload = *get_if<BuiltinFunctionType>(*value);
    return builtin(payload.builtin, payload.owner);
  }
  case TypeKind::Box:
    return box(value->element());
  case TypeKind::Vec:
    return vector(value->element());
  case TypeKind::Array:
    return array(value->element(), value->length());
  case TypeKind::Ref:
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
      ast::Overloaded{
          [](const ErrorType &) { return true; },
          [](const NeverType &) { return true; },
          [](const UnitType &) { return true; },
          [](const BoolType &) { return true; },
          [](const I32Type &) { return true; },
          [](const U32Type &) { return true; },
          [](const IsizeType &) { return true; },
          [](const UsizeType &) { return true; },
          [&](const StructType &payload) {
            return payload.symbol == get_if<StructType>(*right)->symbol;
          },
          [&](const BoxType &payload) {
            return same_type(payload.element, right->element());
          },
          [&](const VecType &payload) {
            return same_type(payload.element, right->element());
          },
          [&](const ArrayType &payload) {
            return payload.length == right->length() &&
                   same_type(payload.element, right->element());
          },
          [&](const RefType &payload) {
            return payload.mutability == get_if<RefType>(*right)->mutability &&
                   same_type(payload.element, right->element());
          },
          [&](const FunctionType &payload) {
            return payload.symbol == get_if<FunctionType>(*right)->symbol;
          },
          [&](const BuiltinFunctionType &payload) {
            const auto &other = *get_if<BuiltinFunctionType>(*right);
            return payload.builtin == other.builtin &&
                   ((!payload.owner && !other.owner) ||
                    same_type(payload.owner, other.owner));
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
          [](const StructType &payload) { return payload.name; },
          [](const BoxType &payload) {
            return "Box<" + type_name(payload.element) + ">";
          },
          [](const VecType &payload) {
            return "Vec<" + type_name(payload.element) + ">";
          },
          [](const ArrayType &payload) {
            return "[" + type_name(payload.element) + "; " +
                   std::to_string(payload.length) + "]";
          },
          [](const RefType &payload) {
            std::string name = "&";
            if (payload.mutability == ast::Mutability::Mutable)
              name += "mut ";
            return name + type_name(payload.element);
          },
          [](const FunctionType &payload) { return "fn " + payload.name; },
          [](const BuiltinFunctionType &payload) {
            auto name = "fn " + std::string(builtin_name(payload.builtin));
            if (payload.owner)
              name += " (owner " + type_name(payload.owner) + ")";
            return name;
          }},
      type->data());
}
} // namespace rx
