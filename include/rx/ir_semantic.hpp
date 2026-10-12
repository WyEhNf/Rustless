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
// Indirect is an address of by-value aggregate storage, not a source reference.
// This internal convention is independent of the platform's C aggregate ABI.
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
struct ScalarArithmetic {
  BinaryOp op;
  Type operand_type;
  // Lowering must mask the RHS before emitting a shift instruction.
  std::optional<std::uint32_t> shift_count_mask;
  bool operator==(const ScalarArithmetic &) const = default;
};
struct ScalarComparison {
  Predicate predicate;
  Type operand_type;
  bool operator==(const ScalarComparison &) const = default;
};
struct StructuralEquality {
  SemanticTypePtr compared_type;
  bool negated;
};
using LoweredOperation =
    std::variant<ScalarArithmetic, ScalarComparison, ShortCircuit,
                 StructuralEquality>;
struct BinaryLowering {
  LoweredOperation operation;
  OperandSemantics left;
  OperandSemantics right;
  Type result_type;
};
struct CompoundLowering {
  ScalarArithmetic operation;
  OperandSemantics target;
  OperandSemantics value;
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
  CompoundLowering compound(const ast::Expr &node,
                           const ast::CompoundAssignExpr &payload) const;

private:
  StructLayout &resolve_structure(SymbolId symbol);
  Layout compute_layout(const SemanticTypePtr &type);
  AbiValue abi(const SemanticTypePtr &type) const;
  BinaryLowering lower_binary(const ResolvedBinary &resolved) const;
  const SemanticIndex &semantic_;
  const TargetLayout target_;
  std::map<SymbolId, StructLayout> layouts_;
  std::set<SymbolId> resolving_;
};
} // namespace rx::ir
