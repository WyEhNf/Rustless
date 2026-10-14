#include "rx/semantic/safety/safe_gate.hpp"
#include "rx/ast/visitor.hpp"
#include <functional>
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
SafeGate::SafeGate(const ast::Crate &crate, const SemanticResult &result)
    : SafeGate(crate, result.snapshot(), ValidationTag{}) {}
SafeGate::SafeGate(const ast::Crate &crate,
                   std::shared_ptr<const SemanticData> snapshot, ValidationTag)
    : crate_(&crate), snapshot_(std::move(snapshot)) {
  if (!snapshot_ || !snapshot_->success || snapshot_->source_crate != &crate)
    throw std::invalid_argument(
        "semantic result is not checked for this crate");
  const auto &data = *snapshot_;
  for (const auto &fact : data.symbols) {
    insert(symbols_, fact.id, fact);
    insert(declarations_, fact.declaration, fact);
  }
  for (const auto &fact : data.expressions)
    insert(expressions_, fact.node, fact);
  for (const auto &fact : data.resolved_types)
    insert(types_, fact.node, fact);
  for (const auto &fact : data.symbol_types)
    insert(symbol_types_, fact.symbol, fact);
  for (const auto &fact : data.functions)
    insert(functions_, fact.symbol, fact);
  for (const auto &fact : data.structures)
    insert(structures_, fact.symbol, fact);
  for (const auto &fact : data.constants)
    insert(constants_, fact.symbol, fact);
  validate_links();
  validate_tree();
  validate_declarations();
  validate_shapes();
}
const SymbolRecord &SafeGate::symbol(SymbolId id) const {
  return *symbols_.at(id);
}
const SymbolRecord &SafeGate::declaration(ast::NodeId id) const {
  return *declarations_.at(id);
}
const ExpressionSemantics &SafeGate::expression(ast::NodeId id) const {
  return *expressions_.at(id);
}
const SemanticTypePtr &SafeGate::type(ast::NodeId id) const {
  return types_.at(id)->type;
}
const SemanticTypePtr &SafeGate::symbol_type(SymbolId id) const {
  return symbol_types_.at(id)->type;
}
const FunctionSemantics &SafeGate::function(SymbolId id) const {
  return *functions_.at(id);
}
const StructSemantics &SafeGate::structure(SymbolId id) const {
  return *structures_.at(id);
}
const ConstantValue &SafeGate::constant(SymbolId id) const {
  return *constants_.at(id);
}
void SafeGate::require_node(const ast::Node &node) const {
  const auto found = nodes_.find(node.id);
  if (found == nodes_.end() || found->second != &node)
    throw std::invalid_argument("semantic query uses a foreign AST node");
}

} // namespace rx
