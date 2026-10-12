#pragma once

#include "rx/semantic.hpp"
#include <unordered_map>

namespace rx {
// The index owns a frozen semantic snapshot and borrows its original AST.
// The AST must remain alive and unchanged; the result wrapper may be destroyed.
class SemanticIndex {
public:
  SemanticIndex(const ast::Crate &crate, const SemanticResult &result);
  SemanticIndex(ast::Crate &&, const SemanticResult &) = delete;
  const ast::Crate &crate() const { return *crate_; }
  const SymbolRecord &symbol(SymbolId id) const;
  const SymbolRecord &declaration(ast::NodeId id) const;
  const SymbolRecord &declaration(const ast::Node &node) const {
    require_node(node);
    return declaration(node.id);
  }
  const ExpressionSemantics &expression(ast::NodeId id) const;
  const SemanticTypePtr &type(ast::NodeId id) const;
  const SemanticTypePtr &symbol_type(SymbolId id) const;
  const FunctionSemantics &function(SymbolId id) const;
  const StructSemantics &structure(SymbolId id) const;
  const ConstantValue &constant(SymbolId id) const;
  const SemanticTypePtr &type(const ast::TypeSyntax &node) const {
    require_node(node);
    return type(node.id);
  }
  const ExpressionSemantics &expression(const ast::Expr &node) const {
    require_node(node);
    return expression(node.id);
  }
  const ExpressionSemantics &expression(const ast::Block &node) const {
    require_node(node);
    return expression(node.id);
  }
  const ExpressionSemantics &expression(const ast::IfBranch &node) const {
    require_node(node);
    return expression(node.id);
  }

private:
  friend class SemanticResult;
  struct ValidationTag {};
  SemanticIndex(const ast::Crate &crate,
                std::shared_ptr<const SemanticData> snapshot, ValidationTag);
  void validate_links() const;
  void validate_tree();
  void validate_declarations() const;
  void validate_shapes() const;
  void require_node(const ast::Node &node) const;
  const ast::Crate *crate_;
  std::shared_ptr<const SemanticData> snapshot_;
  std::unordered_map<ast::NodeId, const ast::Node *> nodes_;
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
