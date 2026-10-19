#pragma once

#include "rx/llvm/facts.hpp"
#include "rx/llvm/ir/ir.hpp"
#include "rx/llvm/ir/target.hpp"
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
struct FunctionCall {
  FunctionAbi signature;
};
struct BuiltinCall {
  BuiltinId builtin;
};
struct CallLowering {
  std::variant<FunctionCall, BuiltinCall> target;
  std::optional<OperandSemantics> receiver;
  std::vector<OperandSemantics> arguments;
  SemanticTypePtr result;
};
enum class ShortCircuit { And, Or };
struct ScalarArithmetic {
  BinaryOp op;
  Type operand_type;

  std::optional<std::uint32_t> shift_count_mask;
  bool operator==(const ScalarArithmetic &other) const {
    return op == other.op && operand_type == other.operand_type &&
           shift_count_mask == other.shift_count_mask;
  }
  bool operator!=(const ScalarArithmetic &other) const { return !(*this == other); }
};
struct ScalarComparison {
  Predicate predicate;
  Type operand_type;
  bool operator==(const ScalarComparison &other) const {
    return predicate == other.predicate && operand_type == other.operand_type;
  }
  bool operator!=(const ScalarComparison &other) const { return !(*this == other); }
};
struct StructuralEquality {
  SemanticTypePtr compared_type;
  bool negated;
  bool operator==(const StructuralEquality &other) const {
    return negated == other.negated && same_type(compared_type, other.compared_type);
  }
  bool operator!=(const StructuralEquality &other) const { return !(*this == other); }
};
using LoweredOperation =
    std::variant<ScalarArithmetic, ScalarComparison, ShortCircuit, StructuralEquality>;
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
struct ScalarUnary {
  BinaryOp op;
  Type operand_type;

  std::uint32_t left_constant;
};
struct DereferencePlace {
  SemanticTypePtr element_type;
  Layout layout;
};
struct UnaryLowering {
  std::variant<ScalarUnary, DereferencePlace> operation;
  OperandSemantics operand;
  SemanticTypePtr result;
};
class SemanticBridge {
public:
  explicit SemanticBridge(const SemanticData &semantic, TargetLayout target = TargetLayout::rv32());
  const TargetLayout &target() const { return target_; }
  const SemanticFacts &facts() const { return semantic_; }
  Layout layout(const SemanticTypePtr &type) const;
  std::optional<Type> scalar_type(const SemanticTypePtr &type) const;
  const StructLayout &structure(SymbolId symbol) const { return layouts_.at(symbol); }
  FunctionAbi function(const ast::Function &node) const;
  FunctionAbi function(SymbolId symbol) const;
  Declaration declaration(const ast::Function &node) const;
  std::vector<Declaration> declarations() const;
  BinaryLowering binary(const ast::Expr &node, const ast::BinaryExpr &payload) const;
  CompoundLowering compound(const ast::Expr &node, const ast::CompoundAssignExpr &payload) const;
  CallLowering call(const ast::Expr &node, const ast::CallExpr &payload) const;
  CallLowering call(const ast::Expr &node, const ast::MethodCallExpr &payload) const;
  UnaryLowering unary(const ast::Expr &node, const ast::UnaryExpr &payload) const;

private:
  StructLayout &resolve_structure(SymbolId symbol);
  Layout compute_layout(const SemanticTypePtr &type);
  AbiValue abi(const SemanticTypePtr &type) const;
  BinaryLowering lower_binary(const ResolvedBinary &resolved) const;
  CallLowering lower_call(const ast::Expr &node, const std::vector<ast::ExprPtr> &arguments) const;
  SemanticFacts semantic_;
  const TargetLayout target_;
  std::map<SymbolId, StructLayout> layouts_;
  std::set<SymbolId> resolving_;
};
}
