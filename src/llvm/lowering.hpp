#pragma once

#include "rx/llvm/ir/builder.hpp"
#include "rx/llvm/lower.hpp"
#include "rx/llvm/bridge/semantic.hpp"
#include "rx/llvm/value.hpp"
#include <functional>
#include <map>
#include <unordered_map>

namespace rx::ir {

enum class HelperKind { Clone, Equal };
struct HelperRequest {
  HelperKind kind;
  SemanticTypePtr type;
  std::string name;
};

class ModuleLowering {
public:
  explicit ModuleLowering(const SemanticData &semantic) : bridge(semantic) {}
  Module run();
  std::string helper(HelperKind kind, const SemanticTypePtr &type);
  std::string function_name(SymbolId symbol) const;
  SemanticBridge bridge;
  Module module;

private:
  std::vector<HelperRequest> helpers_;
  std::map<std::pair<HelperKind, std::string>, std::string> helper_names_;
};

class FunctionLowering {
public:
  FunctionLowering(ModuleLowering &module, std::string name, Type result);
  Function take() { return std::move(function_); }
  void body(const ast::Function &node);
  void helper_body(const HelperRequest &request);

private:
  Operand parameter(Type type, std::string name, std::optional<SymbolId> symbol = {});
  Value expression(const ast::Expr &node);
  Value intrinsic(const ast::Expr &node);
  Value block(const ast::Block &node);
  Value branch(const ast::Node &node, const ast::IfExpr &payload);
  Value short_circuit(
      const ast::Expr &node, const ast::BinaryExpr &payload, ShortCircuit operation);
  Value freeze(Value value);
  Value adjust(Value value, const std::vector<Adjustment> &adjustments);
  Value convert(Value value, const SemanticTypePtr &target);
  Value dereference(Value value);
  Operand scalar(Value value);
  Operand address(Value value);
  Operand storage(const SemanticTypePtr &type);
  Operand offset(Operand base, std::uint32_t bytes);
  Operand offset(Operand base, Operand bytes);
  Value read(const SemanticTypePtr &type, Operand source);
  Value temporary(const SemanticTypePtr &type, Operand source);
  void write(const SemanticTypePtr &type, Operand destination, Value source);
  void copy(Operand destination, Operand source, std::uint32_t bytes);
  void return_value(Value value);
  void bind(SymbolId symbol, const SemanticTypePtr &type, Value value);
  Value apply_binary(
      const BinaryLowering &operation, Value left, Value right, const SemanticTypePtr &result_type);
  Value invoke(const CallLowering &call, std::vector<Value> arguments);
  Value builtin(BuiltinId builtin, std::vector<Value> arguments, const SemanticTypePtr &result);
  Value vector_push(Value receiver, Value element, const SemanticTypePtr &result);
  Value vector_remove(Value receiver, Value index, const SemanticTypePtr &result);
  Value clone(Value source, const SemanticTypePtr &type);
  void each(Operand count, const std::function<void(Operand)> &body);
  void clone_body(const SemanticTypePtr &type, Operand destination, Operand source);
  void equality_body(const SemanticTypePtr &type, Operand left, Operand right);

  Value emit(const ast::Expr &node, const ast::IntegerExpr &payload);
  Value emit(const ast::Expr &node, const ast::BooleanExpr &payload);
  Value emit(const ast::Expr &node, const ast::UnitExpr &payload);
  Value emit(const ast::Expr &node, const ast::PathExpr &payload);
  Value emit(const ast::Expr &node, const ast::ParenExpr &payload);
  Value emit(const ast::Expr &node, const ast::ArrayExpr &payload);
  Value emit(const ast::Expr &node, const ast::RepeatArrayExpr &payload);
  Value emit(const ast::Expr &node, const ast::StructExpr &payload);
  Value emit(const ast::Expr &node, const ast::BlockExpr &payload);
  Value emit(const ast::Expr &node, const ast::IfExpr &payload);
  Value emit(const ast::Expr &node, const ast::LoopExpr &payload);
  Value emit(const ast::Expr &node, const ast::WhileExpr &payload);
  Value emit(const ast::Expr &node, const ast::UnaryExpr &payload);
  Value emit(const ast::Expr &node, const ast::BorrowExpr &payload);
  Value emit(const ast::Expr &node, const ast::BinaryExpr &payload);
  Value emit(const ast::Expr &node, const ast::AssignExpr &payload);
  Value emit(const ast::Expr &node, const ast::CompoundAssignExpr &payload);
  Value emit(const ast::Expr &node, const ast::CastExpr &payload);
  Value emit(const ast::Expr &node, const ast::CallExpr &payload);
  Value emit(const ast::Expr &node, const ast::MethodCallExpr &payload);
  Value emit(const ast::Expr &node, const ast::FieldExpr &payload);
  Value emit(const ast::Expr &node, const ast::IndexExpr &payload);
  Value emit(const ast::Expr &node, const ast::BreakExpr &payload);
  Value emit(const ast::Expr &node, const ast::ContinueExpr &payload);
  Value emit(const ast::Expr &node, const ast::ReturnExpr &payload);

  const SemanticFacts &facts() const { return module_.bridge.facts(); }
  const SemanticTypePtr &type(const ast::Node &node) const { return facts().expression(node).type; }
  Value unit(const SemanticTypePtr &type) const { return {type, UnitValue{}}; }

  struct Loop {
    BlockId next;
    BlockId exit;
    SemanticTypePtr type;
    std::optional<Operand> output;
    bool has_break{};
  };
  ModuleLowering &module_;
  Function function_;
  Builder builder_;
  std::unordered_map<SymbolId, Value> bindings_;
  SemanticTypePtr result_type_;
  std::optional<Operand> output_;
  std::vector<Loop> loops_;
};

void add_runtime(Module &module);

}
