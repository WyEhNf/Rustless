#include "rx/ir_semantic.hpp"
#include <algorithm>
#include <limits>
#include <stdexcept>

namespace rx::ir {
namespace {
using K = SemanticType::Kind;
std::uint32_t checked_size(std::uint64_t size) {
  if (size > std::numeric_limits<std::uint32_t>::max())
    throw std::overflow_error("type layout exceeds RV32 address space");
  return static_cast<std::uint32_t>(size);
}
std::uint32_t align_size(std::uint32_t size, std::uint32_t alignment) {
  return checked_size((std::uint64_t{size} + alignment - 1) / alignment * alignment);
}
Layout array_layout(Layout element, std::uint64_t count) {
  if (element.bytes && count > std::numeric_limits<std::uint32_t>::max() / element.bytes)
    throw std::overflow_error("array layout exceeds RV32 address space");
  return {static_cast<std::uint32_t>(element.bytes * count), element.alignment};
}
} // namespace
SemanticBridge::SemanticBridge(const SemanticIndex &semantic) : semantic_(semantic) {
  for (const auto &item : semantic.crate().items)
    ast::visit(item, ast::Overloaded{
      [&](const ast::Struct &node, const ast::Struct &) {
        structures_.emplace(node.name.text, semantic.declaration(node.id).id);
      },
      [](const ast::Use &, const ast::Use &) {},
      [](const ast::Function &, const ast::Function &) {},
      [](const ast::Constant &, const ast::Constant &) {},
      [](const ast::Impl &, const ast::Impl &) {}});
  for (const auto &[_, symbol] : structures_) resolve_structure(symbol);
}
std::optional<Type> SemanticBridge::scalar_type(const SemanticTypePtr &type) const {
  if (!type) throw std::invalid_argument("missing checked type");
  switch (type->kind) {
  case K::Unit: case K::Never: return Type::Void;
  case K::Bool: return Type::I1;
  case K::I32: case K::U32: case K::Isize: case K::Usize: return Type::I32;
  case K::Ref: case K::Box: case K::Function: return Type::Ptr;
  case K::Array: case K::Struct: case K::Vec: return {};
  case K::Error: break;
  }
  throw std::invalid_argument("cannot lower an invalid semantic type");
}
Layout SemanticBridge::layout(const SemanticTypePtr &type) const {
  if (!type) throw std::invalid_argument("missing checked type");
  switch (type->kind) {
  case K::Unit: case K::Never: return {0, 1};
  case K::Bool: return {1, 1};
  case K::Vec: return {12, 4};
  case K::Array: return array_layout(layout(type->element), type->length);
  case K::Struct: return layouts_.at(structures_.at(type->name)).layout;
  case K::I32: case K::U32: case K::Isize: case K::Usize:
  case K::Ref: case K::Box: case K::Function: return {4, 4};
  case K::Error: break;
  }
  throw std::invalid_argument("cannot lay out an invalid semantic type");
}
Layout SemanticBridge::compute_layout(const SemanticTypePtr &type) {
  if (!type) throw std::invalid_argument("missing checked type");
  if (type->kind == K::Struct) return resolve_structure(structures_.at(type->name)).layout;
  if (type->kind == K::Array) return array_layout(compute_layout(type->element), type->length);
  return layout(type);
}
StructLayout &SemanticBridge::resolve_structure(SymbolId symbol) {
  if (auto found = layouts_.find(symbol); found != layouts_.end()) return found->second;
  if (!resolving_.insert(symbol).second) throw std::invalid_argument("inline recursive layout");
  StructLayout result{symbol, {0, 1}, {}};
  for (auto field : semantic_.structure(symbol).fields) {
    auto value = compute_layout(semantic_.symbol_type(field));
    auto offset = align_size(result.layout.bytes, value.alignment);
    result.fields.push_back({field, offset, value});
    result.layout.bytes = checked_size(std::uint64_t{offset} + value.bytes);
    result.layout.alignment = std::max(result.layout.alignment, value.alignment);
  }
  result.layout.bytes = align_size(result.layout.bytes, result.layout.alignment);
  resolving_.erase(symbol);
  return layouts_.emplace(symbol, std::move(result)).first->second;
}
} // namespace rx::ir
