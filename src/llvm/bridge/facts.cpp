#include "rx/llvm/bridge/facts.hpp"

namespace rx::ir {

SemanticFacts::SemanticFacts(const SemanticData &data) : data_(data) {
  for (const auto &symbol : data.symbols) {
    symbols_.emplace(symbol.id, &symbol);
    declarations_.emplace(symbol.declaration, &symbol);
  }
  for (const auto &expression : data.expressions)
    expressions_.emplace(expression.node, &expression);
  for (const auto &type : data.resolved_types)
    types_.emplace(type.node, &type);
  for (const auto &type : data.symbol_types)
    symbol_types_.emplace(type.symbol, &type);
  for (const auto &function : data.functions)
    functions_.emplace(function.symbol, &function);
  for (const auto &structure : data.structures)
    structures_.emplace(structure.symbol, &structure);
  for (const auto &constant : data.constants)
    constants_.emplace(constant.symbol, &constant);
}

}
