#pragma once

#include "rx/semantic/semantic.hpp"
#include <unordered_map>

namespace rx::ir {

class SemanticFacts {
public:
  explicit SemanticFacts(const SemanticData &data);
  const ast::Crate &crate() const { return *data_.source_crate; }
  const SymbolRecord &symbol(SymbolId id) const { return *symbols_.at(id); }
  const SymbolRecord &declaration(ast::NodeId id) const { return *declarations_.at(id); }
  const SymbolRecord &declaration(const ast::Node &node) const { return declaration(node.id); }
  const ExpressionSemantics &expression(ast::NodeId id) const { return *expressions_.at(id); }
  const ExpressionSemantics &expression(const ast::Node &node) const { return expression(node.id); }
  const SemanticTypePtr &type(ast::NodeId id) const { return types_.at(id)->type; }
  const SemanticTypePtr &symbol_type(SymbolId id) const { return symbol_types_.at(id)->type; }
  const FunctionSemantics &function(SymbolId id) const { return *functions_.at(id); }
  const StructSemantics &structure(SymbolId id) const { return *structures_.at(id); }
  const ConstantValue &constant(SymbolId id) const { return *constants_.at(id); }

private:
  const SemanticData &data_;
  std::unordered_map<SymbolId, const SymbolRecord *> symbols_;
  std::unordered_map<ast::NodeId, const SymbolRecord *> declarations_;
  std::unordered_map<ast::NodeId, const ExpressionSemantics *> expressions_;
  std::unordered_map<ast::NodeId, const TypeResolution *> types_;
  std::unordered_map<SymbolId, const SymbolType *> symbol_types_;
  std::unordered_map<SymbolId, const FunctionSemantics *> functions_;
  std::unordered_map<SymbolId, const StructSemantics *> structures_;
  std::unordered_map<SymbolId, const ConstantValue *> constants_;
};

}
