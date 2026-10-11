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
// Link and tree validation are strengthened in the following slices.
void SemanticIndex::validate_links() const {}
void SemanticIndex::validate_tree() const {}
} // namespace rx
