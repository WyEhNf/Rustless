#include "checks.hpp"

namespace rx {

using safety_detail::require_equivalent;

void SafeGate::validate_declarations() const {
  std::unordered_set<ast::NodeId> expected;
  auto require_symbol = [&](const ast::Node &node, SymbolKind kind,
                            std::optional<SymbolId> owner = {},
                            std::string_view name = {}) -> SymbolId {
    const auto &record = declaration(node.id);
    if (record.kind != kind || record.owner != owner ||
        (!name.empty() && record.name != name))
      throw std::invalid_argument("AST declaration kind or owner mismatch");
    expected.insert(node.id);
    return record.id;
  };
  auto check_struct = [&](const ast::Struct &node) {
    const auto id =
        require_symbol(node, SymbolKind::Struct, {}, node.name.text);
    const auto &fact = structure(id);
    if (fact.fields.size() != node.fields.size())
      throw std::invalid_argument("semantic struct field count mismatch");
    for (std::size_t index = 0; index < node.fields.size(); ++index) {
      const auto &field = node.fields[index];
      const auto field_id =
          require_symbol(field, SymbolKind::Field, id, field.name.text);
      if (fact.fields[index] != field_id)
        throw std::invalid_argument("semantic struct field order mismatch");
      require_equivalent(symbol_type(field_id), type(*field.type));
    }
  };
  auto check_function = [&](const ast::Function &node,
                            std::optional<SymbolId> owner = {}) {
    auto kind = SymbolKind::Function;
    auto name = node.name.text;
    if (owner) {
      kind = SymbolKind::AssociatedFunction;
      name = symbol(*owner).name + "::" + name;
    }
    const auto id = require_symbol(node, kind, owner, name);
    const auto &fact = function(id);
    if (fact.parameters.size() != node.signature.parameters.size() ||
        fact.receiver.has_value() != node.signature.receiver.has_value())
      throw std::invalid_argument("semantic function signature count mismatch");
    for (std::size_t index = 0; index < node.signature.parameters.size();
         ++index) {
      const auto &parameter = node.signature.parameters[index];
      const auto parameter_id = require_symbol(parameter, SymbolKind::Parameter,
                                               id, parameter.binding.name.text);
      if (fact.parameters[index].symbol != parameter_id)
        throw std::invalid_argument("semantic parameter order mismatch");
      require_equivalent(fact.parameters[index].type, type(*parameter.type));
      require_equivalent(fact.parameters[index].type,
                         symbol_type(parameter_id));
    }
    if (fact.receiver) {
      const auto &receiver = *node.signature.receiver;
      const auto receiver_id =
          require_symbol(receiver, SymbolKind::Receiver, id, "self");
      if (!owner || fact.receiver->symbol != receiver_id)
        throw std::invalid_argument("semantic receiver identity mismatch");
      require_equivalent(fact.receiver->type, symbol_type(receiver_id));
      auto receiver_type = fact.receiver->type;
      if (ast::by_reference(receiver)) {
        if (receiver_type->kind() != SemanticType::Kind::Ref ||
            receiver_type->is_mutable() != ast::is_mutable(receiver))
          throw std::invalid_argument("semantic receiver reference mismatch");
        receiver_type = receiver_type->element();
      }
      if (receiver_type->kind() != SemanticType::Kind::Struct ||
          receiver_type->symbol() != *owner)
        throw std::invalid_argument("semantic receiver owner type mismatch");
    }
    if (node.signature.return_annotation)
      require_equivalent(fact.result, type(**node.signature.return_annotation));
    else if (fact.result->kind() != SemanticType::Kind::Unit)
      throw std::invalid_argument("unannotated function result is not unit");
    ast::walk(*node.body, [&](const auto &statement, const auto &payload) {
      if constexpr (std::is_same_v<std::decay_t<decltype(payload)>,
                                   ast::LetStmt>) {
        const auto local = require_symbol(statement, SymbolKind::Local, id,
                                          payload.binding.name.text);
        if (payload.annotation)
          require_equivalent(symbol_type(local), type(**payload.annotation));
        else
          require_equivalent(symbol_type(local),
                             expression(*payload.initializer).type);
      }
    });
  };
  auto check_constant = [&](const ast::Constant &node,
                            std::optional<SymbolId> owner = {}) {
    auto kind = SymbolKind::Constant;
    auto name = node.name.text;
    if (owner) {
      kind = SymbolKind::AssociatedConstant;
      name = symbol(*owner).name + "::" + name;
    }
    const auto id = require_symbol(node, kind, owner, name);
    require_equivalent(symbol_type(id), type(*node.type));
    require_equivalent(symbol_type(id), expression(*node.value).type);
    (void)constant(id);
  };
  for (const auto &item : crate_->items)
    ast::visit(item, [&](const ast::Node &, const auto &payload) {
      using Payload = std::decay_t<decltype(payload)>;
      if constexpr (std::is_same_v<Payload, ast::Struct>)
        check_struct(payload);
      if constexpr (std::is_same_v<Payload, ast::Function>)
        check_function(payload);
      if constexpr (std::is_same_v<Payload, ast::Constant>)
        check_constant(payload);
      if constexpr (std::is_same_v<Payload, ast::Impl>) {
        const auto target = type(*payload.target);
        if (target->kind() != SemanticType::Kind::Struct)
          throw std::invalid_argument("checked impl target is not a struct");
        const auto owner = target->symbol();
        for (const auto &member : payload.members)
          ast::visit(member, [&](const ast::Node &, const auto &value) {
            if constexpr (std::is_same_v<std::decay_t<decltype(value)>,
                                         ast::Function>)
              check_function(value, owner);
            if constexpr (std::is_same_v<std::decay_t<decltype(value)>,
                                         ast::Constant>)
              check_constant(value, owner);
          });
      }
    });
  if (expected.size() != declarations_.size())
    throw std::invalid_argument(
        "semantic symbols do not match AST declarations");
}

} // namespace rx
