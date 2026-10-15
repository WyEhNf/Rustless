#include "rx/llvm/semantic.hpp"
#include <algorithm>
#include <stdexcept>

namespace rx::ir {
namespace {
using TypeKind = SemanticType::Kind;
std::uint32_t checked_size(std::uint64_t size, const TargetLayout &target) {
  if (size > target.max_object_size()) {
    throw std::overflow_error("type layout exceeds RV32 address space");
  }
  return static_cast<std::uint32_t>(size);
}
std::uint32_t align_size(std::uint32_t size, std::uint32_t alignment, const TargetLayout &target) {
  return checked_size((std::uint64_t{size} + alignment - 1) / alignment * alignment, target);
}
Layout array_layout(Layout element, std::uint64_t count, const TargetLayout &target) {
  if (element.bytes && count > target.max_object_size() / element.bytes) {
    throw std::overflow_error("array layout exceeds RV32 address space");
  }
  return {static_cast<std::uint32_t>(element.bytes * count), element.alignment};
}
}
SemanticBridge::SemanticBridge(const SemanticData &semantic, TargetLayout target)
    : semantic_(semantic), target_(target) {
  auto collect_layout = ast::Overloaded{
      [&](const ast::Struct &node, const ast::Struct &) {
        resolve_structure(semantic_.declaration(node).id);
      },
      [](const ast::Use &, const ast::Use &) {},
      [](const ast::Function &, const ast::Function &) {},
      [](const ast::Constant &, const ast::Constant &) {},
      [](const ast::Impl &, const ast::Impl &) {},
  };
  for (const auto &item : semantic_.crate().items) {
    ast::visit(item, collect_layout);
  }
}
std::optional<Type> SemanticBridge::scalar_type(const SemanticTypePtr &type) const {
  if (!type) {
    throw std::invalid_argument("missing checked type");
  }
  switch (type->kind()) {
  case TypeKind::Unit:
  case TypeKind::Never:
    return Type::Void;
  case TypeKind::Bool:
    return Type::I1;
  case TypeKind::I32:
  case TypeKind::U32:
  case TypeKind::Isize:
  case TypeKind::Usize:
    return Type::I32;
  case TypeKind::Ref:
  case TypeKind::Box:
  case TypeKind::Function:
  case TypeKind::BuiltinFunction:
    return Type::Ptr;
  case TypeKind::Array:
  case TypeKind::Struct:
  case TypeKind::Vec:
    return {};
  case TypeKind::Error:
    break;
  }
  throw std::invalid_argument("cannot lower an invalid semantic type");
}
Layout SemanticBridge::layout(const SemanticTypePtr &type) const {
  if (!type) {
    throw std::invalid_argument("missing checked type");
  }
  switch (type->kind()) {
  case TypeKind::Unit:
  case TypeKind::Never:
    return target_.unit();
  case TypeKind::Bool:
    return target_.boolean();
  case TypeKind::Vec:
    return target_.vector().storage;
  case TypeKind::Array:
    return array_layout(layout(type->element()), type->length(), target_);
  case TypeKind::Struct:
    return layouts_.at(type->symbol()).layout;
  case TypeKind::I32:
  case TypeKind::U32:
  case TypeKind::Isize:
  case TypeKind::Usize:
    return target_.integer();
  case TypeKind::Ref:
  case TypeKind::Box:
  case TypeKind::Function:
  case TypeKind::BuiltinFunction:
    return target_.pointer();
  case TypeKind::Error:
    break;
  }
  throw std::invalid_argument("cannot lay out an invalid semantic type");
}
Layout SemanticBridge::compute_layout(const SemanticTypePtr &type) {
  if (!type) {
    throw std::invalid_argument("missing checked type");
  }
  if (type->kind() == TypeKind::Struct) {
    return resolve_structure(type->symbol()).layout;
  }
  if (type->kind() == TypeKind::Array) {
    return array_layout(compute_layout(type->element()), type->length(), target_);
  }
  return layout(type);
}
StructLayout &SemanticBridge::resolve_structure(SymbolId symbol) {
  if (auto found = layouts_.find(symbol); found != layouts_.end()) {
    return found->second;
  }
  if (!resolving_.insert(symbol).second) {
    throw std::invalid_argument("inline recursive layout");
  }
  StructLayout result{symbol, target_.unit(), {}};
  for (auto field : semantic_.structure(symbol).fields) {
    auto value = compute_layout(semantic_.symbol_type(field));
    auto offset = align_size(result.layout.bytes, value.alignment, target_);
    result.fields.push_back({field, offset, value});
    result.layout.bytes = checked_size(std::uint64_t{offset} + value.bytes, target_);
    result.layout.alignment = std::max(result.layout.alignment, value.alignment);
  }
  result.layout.bytes = align_size(result.layout.bytes, result.layout.alignment, target_);
  resolving_.erase(symbol);
  return layouts_.emplace(symbol, std::move(result)).first->second;
}
AbiValue SemanticBridge::abi(const SemanticTypePtr &type) const {
  const auto scalar = scalar_type(type);
  const auto storage = layout(type);
  if (scalar) {
    return {*scalar, Passing::Direct, storage};
  }
  return {Type::Ptr, Passing::Indirect, storage};
}
FunctionAbi SemanticBridge::function(const ast::Function &node) const {
  return function(semantic_.declaration(node).id);
}
FunctionAbi SemanticBridge::function(SymbolId symbol) const {
  const auto &signature = semantic_.function(symbol);
  FunctionAbi result{symbol, semantic_.symbol(symbol).name, abi(signature.result), {}, {}};
  if (signature.receiver) {
    result.receiver = AbiParameter{signature.receiver->symbol, abi(signature.receiver->type)};
  }
  for (const auto &parameter : signature.parameters) {
    result.parameters.push_back({parameter.symbol, abi(parameter.type)});
  }
  return result;
}
Declaration SemanticBridge::declaration(const ast::Function &node) const {
  auto signature = function(node);
  Type return_type = signature.result.type;
  if (signature.result.passing == Passing::Indirect) {
    return_type = Type::Void;
  }
  Declaration result{signature.name, return_type, {}, signature.symbol, ir::origin(node)};

  if (signature.result.passing == Passing::Indirect) {
    result.parameters.push_back(Type::Ptr);
  }
  auto append = [&](const AbiParameter &parameter) {
    if (parameter.value.type != Type::Void) {
      result.parameters.push_back(parameter.value.type);
    }
  };
  if (signature.receiver) {
    append(*signature.receiver);
  }
  for (const auto &parameter : signature.parameters) {
    append(parameter);
  }
  return result;
}
std::vector<Declaration> SemanticBridge::declarations() const {
  std::vector<Declaration> result;
  auto append = [&](const ast::Function &node, const ast::Function &) {
    result.push_back(declaration(node));
  };
  auto collect_member = ast::Overloaded{
      append,
      [](const ast::Constant &, const ast::Constant &) {},
  };
  auto collect_item = ast::Overloaded{
      append,
      [&](const ast::Impl &node, const ast::Impl &) {
        for (const auto &member : node.members) {
          ast::visit(member, collect_member);
        }
      },
      [](const ast::Use &, const ast::Use &) {},
      [](const ast::Struct &, const ast::Struct &) {},
      [](const ast::Constant &, const ast::Constant &) {},
  };
  for (const auto &item : semantic_.crate().items) {
    ast::visit(item, collect_item);
  }
  return result;
}
}
