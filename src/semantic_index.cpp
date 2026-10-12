#include "rx/semantic_index.hpp"
#include "rx/ast/visitor.hpp"
#include <stdexcept>
#include <unordered_set>

namespace rx {
namespace {
template <class Map, class Id, class Fact>
void insert(Map &map, Id id, const Fact &fact) {
  if (!id.value || !map.emplace(id, &fact).second)
    throw std::invalid_argument("duplicate or zero semantic identity");
}
} // namespace
SemanticIndex::SemanticIndex(const ast::Crate &crate,
                             const SemanticResult &result)
    : crate_(&crate) {
  if (!result.success || result.source_crate != &crate)
    throw std::invalid_argument(
        "semantic result is not checked for this crate");
  for (const auto &fact : result.symbols) {
    insert(symbols_, fact.id, fact);
    insert(declarations_, fact.declaration, fact);
  }
  for (const auto &fact : result.expressions)
    insert(expressions_, fact.node, fact);
  for (const auto &fact : result.resolved_types)
    insert(types_, fact.node, fact);
  for (const auto &fact : result.symbol_types)
    insert(symbol_types_, fact.symbol, fact);
  for (const auto &fact : result.functions)
    insert(functions_, fact.symbol, fact);
  for (const auto &fact : result.structures)
    insert(structures_, fact.symbol, fact);
  for (const auto &fact : result.constants)
    insert(constants_, fact.symbol, fact);
  validate_links();
  validate_tree();
  validate_declarations();
}
const SymbolRecord &SemanticIndex::symbol(SymbolId id) const {
  return *symbols_.at(id);
}
const SymbolRecord &SemanticIndex::declaration(ast::NodeId id) const {
  return *declarations_.at(id);
}
const ExpressionSemantics &SemanticIndex::expression(ast::NodeId id) const {
  return *expressions_.at(id);
}
const SemanticTypePtr &SemanticIndex::type(ast::NodeId id) const {
  return types_.at(id)->type;
}
const SemanticTypePtr &SemanticIndex::symbol_type(SymbolId id) const {
  return symbol_types_.at(id)->type;
}
const FunctionSemantics &SemanticIndex::function(SymbolId id) const {
  return *functions_.at(id);
}
const StructSemantics &SemanticIndex::structure(SymbolId id) const {
  return *structures_.at(id);
}
const ConstantValue &SemanticIndex::constant(SymbolId id) const {
  return *constants_.at(id);
}
void SemanticIndex::validate_links() const {
  auto require_type = [&](const SemanticTypePtr &type) {
    if (!type || type->kind == SemanticType::Kind::Error)
      throw std::invalid_argument("checked semantic fact has no valid type");
  };
  for (const auto &[_, fact] : symbols_) {
    if (fact->owner)
      (void)symbol(*fact->owner);
    switch (fact->kind) {
    case SymbolKind::Struct:
      (void)structure(fact->id);
      break;
    case SymbolKind::Function:
    case SymbolKind::AssociatedFunction:
      (void)function(fact->id);
      break;
    case SymbolKind::Constant:
    case SymbolKind::AssociatedConstant:
      (void)constant(fact->id);
      require_type(symbol_type(fact->id));
      break;
    default:
      require_type(symbol_type(fact->id));
      break;
    }
  }
  for (const auto &[_, fact] : expressions_) {
    require_type(fact->type);
    if (fact->symbol)
      (void)symbol(*fact->symbol);
    for (const auto &adjustment : fact->adjustments)
      require_type(adjustment.target);
  }
  for (const auto &[_, fact] : types_)
    require_type(fact->type);
  for (const auto &[id, fact] : symbol_types_) {
    (void)symbol(id);
    require_type(fact->type);
  }
  for (const auto &[id, fact] : functions_) {
    auto kind = symbol(id).kind;
    if (kind != SymbolKind::Function && kind != SymbolKind::AssociatedFunction)
      throw std::invalid_argument(
          "function fact links to a non-function symbol");
    require_type(fact->result);
    auto parameter = [&](const ParameterSemantics &value, SymbolKind expected) {
      const auto &record = symbol(value.symbol);
      if (record.kind != expected || record.owner != id)
        throw std::invalid_argument("parameter owner or kind mismatch");
      require_type(value.type);
    };
    if (fact->receiver)
      parameter(*fact->receiver, SymbolKind::Receiver);
    for (const auto &value : fact->parameters)
      parameter(value, SymbolKind::Parameter);
  }
  for (const auto &[id, fact] : structures_) {
    if (symbol(id).kind != SymbolKind::Struct)
      throw std::invalid_argument("struct fact links to a non-struct symbol");
    std::unordered_set<SymbolId> seen;
    for (auto field : fact->fields) {
      const auto &record = symbol(field);
      if (record.kind != SymbolKind::Field || record.owner != id ||
          !seen.insert(field).second)
        throw std::invalid_argument("field owner, kind or identity mismatch");
      require_type(symbol_type(field));
    }
  }
  for (const auto &[id, _] : constants_) {
    auto kind = symbol(id).kind;
    if (kind != SymbolKind::Constant && kind != SymbolKind::AssociatedConstant)
      throw std::invalid_argument(
          "constant fact links to a non-constant symbol");
  }
}
void SemanticIndex::validate_tree() const {
  std::unordered_set<ast::NodeId> identities;
  std::unordered_set<ast::NodeId> expression_nodes;
  std::unordered_set<ast::NodeId> type_nodes;
  // The first traversal also includes preserved, not-yet-interpreted
  // constraints.
  ast::walk(*crate_, [&](const ast::Node &node, const auto &) {
    if (!node.id.value || !identities.insert(node.id).second ||
        node.span.file != crate_->source.id ||
        node.span.begin > node.span.end ||
        node.span.end > crate_->source.text.size())
      throw std::invalid_argument("invalid AST identity or source span");
  });
  auto check_ids = [&](const auto &facts) {
    for (const auto &[id, _] : facts)
      if (!identities.contains(id))
        throw std::invalid_argument("semantic fact refers outside its AST");
  };
  check_ids(declarations_);
  check_ids(expressions_);
  check_ids(types_);
  ast::walk(*crate_, [&](const auto &owner, const auto &) {
    using T = std::remove_cvref_t<decltype(owner)>;
    // Lifetime constraints are preserved syntax; current analysis skips them.
    if constexpr (std::same_as<T, ast::TypePredicate>)
      return ast::WalkControl::SkipChildren;
    if constexpr (std::same_as<T, ast::Expr> || std::same_as<T, ast::Block> ||
                  std::same_as<T, ast::IfBranch>) {
      (void)expression(owner.id);
      expression_nodes.insert(owner.id);
    }
    if constexpr (std::same_as<T, ast::TypeSyntax>) {
      (void)type(owner.id);
      type_nodes.insert(owner.id);
    }
    return ast::WalkControl::Continue;
  });
  if (expression_nodes.size() != expressions_.size() ||
      type_nodes.size() != types_.size())
    throw std::invalid_argument("semantic fact is attached to wrong AST shape");
}
namespace {
bool equivalent(const SemanticTypePtr &left, const SemanticTypePtr &right) {
  if (!left || !right || left->kind != right->kind)
    return false;
  using Kind = SemanticType::Kind;
  switch (left->kind) {
  case Kind::Struct:
  case Kind::Function:
    return left->name == right->name;
  case Kind::Array:
    return left->length == right->length &&
           equivalent(left->element, right->element);
  case Kind::Ref:
    return left->is_mutable == right->is_mutable &&
           equivalent(left->element, right->element);
  case Kind::Box:
  case Kind::Vec:
    return equivalent(left->element, right->element);
  default:
    return left->kind != Kind::Error;
  }
}
void require_equivalent(const SemanticTypePtr &left,
                        const SemanticTypePtr &right) {
  if (!equivalent(left, right))
    throw std::invalid_argument("semantic declaration type mismatch");
}
} // namespace
void SemanticIndex::validate_declarations() const {
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
    const auto id = require_symbol(node, SymbolKind::Struct, {}, node.name.text);
    const auto &fact = structure(id);
    if (fact.fields.size() != node.fields.size())
      throw std::invalid_argument("semantic struct field count mismatch");
    for (std::size_t i = 0; i < node.fields.size(); ++i) {
      const auto &field = node.fields[i];
      const auto field_id =
          require_symbol(field, SymbolKind::Field, id, field.name.text);
      if (fact.fields[i] != field_id)
        throw std::invalid_argument("semantic struct field order mismatch");
      require_equivalent(symbol_type(field_id), type(*field.type));
    }
  };
  auto check_function = [&](const ast::Function &node,
                            std::optional<SymbolId> owner = {}) {
    const auto id = require_symbol(
        node, owner ? SymbolKind::AssociatedFunction : SymbolKind::Function,
        owner, owner ? symbol(*owner).name + "::" + node.name.text
                     : node.name.text);
    const auto &fact = function(id);
    if (fact.parameters.size() != node.signature.parameters.size() ||
        fact.receiver.has_value() != node.signature.receiver.has_value())
      throw std::invalid_argument("semantic function signature count mismatch");
    for (std::size_t i = 0; i < node.signature.parameters.size(); ++i) {
      const auto &parameter = node.signature.parameters[i];
      const auto parameter_id =
          require_symbol(parameter, SymbolKind::Parameter, id,
                         parameter.binding.name.text);
      if (fact.parameters[i].symbol != parameter_id)
        throw std::invalid_argument("semantic parameter order mismatch");
      require_equivalent(fact.parameters[i].type, type(*parameter.type));
      require_equivalent(fact.parameters[i].type, symbol_type(parameter_id));
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
        if (receiver_type->kind != SemanticType::Kind::Ref ||
            receiver_type->is_mutable != ast::is_mutable(receiver))
          throw std::invalid_argument("semantic receiver reference mismatch");
        receiver_type = receiver_type->element;
      }
      if (receiver_type->kind != SemanticType::Kind::Struct ||
          receiver_type->name != symbol(*owner).name)
        throw std::invalid_argument("semantic receiver owner type mismatch");
    }
    if (node.signature.return_annotation)
      require_equivalent(fact.result, type(**node.signature.return_annotation));
    else if (fact.result->kind != SemanticType::Kind::Unit)
      throw std::invalid_argument("unannotated function result is not unit");
    ast::walk(*node.body, [&](const auto &statement, const auto &payload) {
      if constexpr (std::same_as<std::remove_cvref_t<decltype(payload)>,
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
    const auto id = require_symbol(
        node, owner ? SymbolKind::AssociatedConstant : SymbolKind::Constant,
        owner, owner ? symbol(*owner).name + "::" + node.name.text
                     : node.name.text);
    require_equivalent(symbol_type(id), type(*node.type));
    require_equivalent(symbol_type(id), expression(*node.value).type);
    (void)constant(id);
  };
  for (const auto &item : crate_->items)
    ast::visit(item, [&](const ast::Node &, const auto &payload) {
      using T = std::remove_cvref_t<decltype(payload)>;
      if constexpr (std::same_as<T, ast::Struct>)
        check_struct(payload);
      if constexpr (std::same_as<T, ast::Function>)
        check_function(payload);
      if constexpr (std::same_as<T, ast::Constant>)
        check_constant(payload);
      if constexpr (std::same_as<T, ast::Impl>) {
        const auto target = type(*payload.target);
        if (target->kind != SemanticType::Kind::Struct)
          throw std::invalid_argument("checked impl target is not a struct");
        SymbolId owner{};
        for (const auto &[id, record] : symbols_)
          if (record->kind == SymbolKind::Struct && record->name == target->name)
            owner = id;
        for (const auto &member : payload.members)
          ast::visit(member, [&](const ast::Node &, const auto &value) {
            if constexpr (std::same_as<std::remove_cvref_t<decltype(value)>,
                                       ast::Function>)
              check_function(value, owner);
            if constexpr (std::same_as<std::remove_cvref_t<decltype(value)>,
                                       ast::Constant>)
              check_constant(value, owner);
          });
      }
    });
  if (expected.size() != declarations_.size())
    throw std::invalid_argument("semantic symbols do not match AST declarations");
}
} // namespace rx
