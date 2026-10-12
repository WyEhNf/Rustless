#pragma once

#include "rx/ir.hpp"
#include "rx/semantic_index.hpp"
#include "rx/target_layout.hpp"
#include <map>
#include <set>

namespace rx::ir {
struct FieldLayout {
  SymbolId symbol;
  std::uint32_t offset;
  Layout layout;
};
struct StructLayout {
  SymbolId symbol;
  Layout layout;
  std::vector<FieldLayout> fields;
};
enum class Passing { Direct, Indirect };
struct AbiValue {
  Type type;
  Passing passing;
  Layout layout;
};
struct AbiParameter {
  SymbolId symbol;
  AbiValue value;
};
struct FunctionAbi {
  SymbolId symbol;
  std::string name;
  AbiValue result;
  std::optional<AbiParameter> receiver;
  std::vector<AbiParameter> parameters;
};
enum class ShortCircuit { And, Or };
struct BinaryLowering {
  std::variant<BinaryOp, Predicate, ShortCircuit> operation;
  Type operand_type;
  Type result_type;
};
class SemanticBridge {
public:
  explicit SemanticBridge(const SemanticIndex &semantic,
                          TargetLayout target = TargetLayout::rv32());
  SemanticBridge(SemanticIndex &&,
                 TargetLayout = TargetLayout::rv32()) = delete;
  const TargetLayout &target() const { return target_; }
  Layout layout(const SemanticTypePtr &type) const;
  std::optional<Type> scalar_type(const SemanticTypePtr &type) const;
  const StructLayout &structure(SymbolId symbol) const {
    return layouts_.at(symbol);
  }
  FunctionAbi function(const ast::Function &node) const;
  Declaration declaration(const ast::Function &node) const;
  std::vector<Declaration> declarations() const;
  BinaryLowering binary(const ast::Expr &node,
                        const ast::BinaryExpr &payload) const;
  BinaryOp compound(const ast::Expr &node,
                    const ast::CompoundAssignExpr &payload) const;

private:
  StructLayout &resolve_structure(SymbolId symbol);
  Layout compute_layout(const SemanticTypePtr &type);
  AbiValue abi(const SemanticTypePtr &type) const;
  const SemanticIndex &semantic_;
  const TargetLayout target_;
  std::map<std::string, SymbolId> structures_;
  std::map<SymbolId, StructLayout> layouts_;
  std::set<SymbolId> resolving_;
};
} // namespace rx::ir
