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
SemanticIndex::SemanticIndex(const ast::Crate &crate, const SemanticResult &result)
    : crate_(&crate) {
  if (!result.success || result.source_crate != &crate)
    throw std::invalid_argument("semantic result is not checked for this crate");
  for (const auto &fact : result.symbols) {
    insert(symbols_, fact.id, fact);
    insert(declarations_, fact.declaration, fact);
  }
  for (const auto &fact : result.expressions) insert(expressions_, fact.node, fact);
  for (const auto &fact : result.resolved_types) insert(types_, fact.node, fact);
  for (const auto &fact : result.symbol_types) insert(symbol_types_, fact.symbol, fact);
  for (const auto &fact : result.functions) insert(functions_, fact.symbol, fact);
  for (const auto &fact : result.structures) insert(structures_, fact.symbol, fact);
  for (const auto &fact : result.constants) insert(constants_, fact.symbol, fact);
  validate_links();
  validate_tree();
}
const SymbolRecord &SemanticIndex::symbol(SymbolId id) const { return *symbols_.at(id); }
const SymbolRecord &SemanticIndex::declaration(ast::NodeId id) const { return *declarations_.at(id); }
const ExpressionSemantics &SemanticIndex::expression(ast::NodeId id) const { return *expressions_.at(id); }
const SemanticTypePtr &SemanticIndex::type(ast::NodeId id) const { return types_.at(id)->type; }
const SemanticTypePtr &SemanticIndex::symbol_type(SymbolId id) const { return symbol_types_.at(id)->type; }
const FunctionSemantics &SemanticIndex::function(SymbolId id) const { return *functions_.at(id); }
const StructSemantics &SemanticIndex::structure(SymbolId id) const { return *structures_.at(id); }
const ConstantValue &SemanticIndex::constant(SymbolId id) const { return *constants_.at(id); }
void SemanticIndex::validate_links() const {
  auto require_type = [&](const SemanticTypePtr &type) {
    if (!type || type->kind == SemanticType::Kind::Error)
      throw std::invalid_argument("checked semantic fact has no valid type");
  };
  for (const auto &[_, fact] : symbols_) {
    if (fact->owner) (void)symbol(*fact->owner);
    switch (fact->kind) {
    case SymbolKind::Struct: (void)structure(fact->id); break;
    case SymbolKind::Function:
    case SymbolKind::AssociatedFunction: (void)function(fact->id); break;
    default: require_type(symbol_type(fact->id)); break;
    }
  }
  for (const auto &[_, fact] : expressions_) {
    require_type(fact->type);
    if (fact->symbol) (void)symbol(*fact->symbol);
    for (const auto &adjustment : fact->adjustments) require_type(adjustment.target);
  }
  for (const auto &[_, fact] : types_) require_type(fact->type);
  for (const auto &[id, fact] : symbol_types_) {
    (void)symbol(id);
    require_type(fact->type);
  }
  for (const auto &[id, fact] : functions_) {
    auto kind = symbol(id).kind;
    if (kind != SymbolKind::Function && kind != SymbolKind::AssociatedFunction)
      throw std::invalid_argument("function fact links to a non-function symbol");
    require_type(fact->result);
    auto parameter = [&](const ParameterSemantics &value, SymbolKind expected) {
      const auto &record = symbol(value.symbol);
      if (record.kind != expected || record.owner != id)
        throw std::invalid_argument("parameter owner or kind mismatch");
      require_type(value.type);
    };
    if (fact->receiver) parameter(*fact->receiver, SymbolKind::Receiver);
    for (const auto &value : fact->parameters) parameter(value, SymbolKind::Parameter);
  }
  for (const auto &[id, fact] : structures_) {
    if (symbol(id).kind != SymbolKind::Struct)
      throw std::invalid_argument("struct fact links to a non-struct symbol");
    std::unordered_set<SymbolId> seen;
    for (auto field : fact->fields) {
      const auto &record = symbol(field);
      if (record.kind != SymbolKind::Field || record.owner != id || !seen.insert(field).second)
        throw std::invalid_argument("field owner, kind or identity mismatch");
      require_type(symbol_type(field));
    }
  }
  for (const auto &[id, _] : constants_) {
    auto kind = symbol(id).kind;
    if (kind != SymbolKind::Constant && kind != SymbolKind::AssociatedConstant)
      throw std::invalid_argument("constant fact links to a non-constant symbol");
  }
}
void SemanticIndex::validate_tree() const {}
} // namespace rx
