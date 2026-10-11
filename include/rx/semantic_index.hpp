#pragma once

#include "rx/semantic.hpp"
#include <unordered_map>

namespace rx {
// A read-only index borrows a successful result and its original AST.
// Both owners must remain alive and unchanged for the index's lifetime.
class SemanticIndex {
public:
  SemanticIndex(const ast::Crate &crate, const SemanticResult &result);
  SemanticIndex(const ast::Crate &, SemanticResult &&) = delete;
  const ast::Crate &crate() const { return *crate_; }
  const SymbolRecord &symbol(SymbolId id) const;
  const SymbolRecord &declaration(ast::NodeId id) const;
  const ExpressionSemantics &expression(ast::NodeId id) const;
  const SemanticTypePtr &type(ast::NodeId id) const;
  const SemanticTypePtr &symbol_type(SymbolId id) const;
  const FunctionSemantics &function(SymbolId id) const;
  const StructSemantics &structure(SymbolId id) const;
  const ConstantValue &constant(SymbolId id) const;
  const SemanticTypePtr &type(const ast::TypeSyntax &node) const {
    return type(node.id);
  }
  const ExpressionSemantics &expression(const ast::Expr &node) const {
    return expression(node.id);
  }
  const ExpressionSemantics &expression(const ast::Block &node) const {
    return expression(node.id);
  }
  const ExpressionSemantics &expression(const ast::IfBranch &node) const {
    return expression(node.id);
  }

private:
  void validate_links() const;
  void validate_tree() const;
  const ast::Crate *crate_;
  std::unordered_map<SymbolId, const SymbolRecord *> symbols_;
  std::unordered_map<ast::NodeId, const SymbolRecord *> declarations_;
  std::unordered_map<ast::NodeId, const ExpressionSemantics *> expressions_;
  std::unordered_map<ast::NodeId, const TypeResolution *> types_;
  std::unordered_map<SymbolId, const SymbolType *> symbol_types_;
  std::unordered_map<SymbolId, const FunctionSemantics *> functions_;
  std::unordered_map<SymbolId, const StructSemantics *> structures_;
  std::unordered_map<SymbolId, const ConstantValue *> constants_;
};
} // namespace rx
