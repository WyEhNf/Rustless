#pragma once

#include "rx/semantic/model/literal.hpp"
#include "semantic/state.hpp"

#include <array>
#include <functional>
#include <map>
#include <set>

namespace rx::semantic_detail {

class Analyzer {
public:
  Analyzer(const ast::Crate &crate, DiagnosticEngine &diagnostics);
  SemanticResult run();

private:
  using EvaluatedConstant = std::pair<SemanticTypePtr, std::int64_t>;

  // Collection and stable symbol identities.
  SymbolId symbol_for(ast::NodeId node);
  void record_symbol(ast::NodeId node, SymbolKind kind, std::string name,
                     std::optional<ast::NodeId> owner = {});
  void record_symbol_type(ast::NodeId declaration, const SemanticTypePtr &type);
  void collect_items();
  static bool builtin_type(std::string_view name);
  static bool protected_builtin_value(std::string_view name);

  // Declaration and type resolution.
  void resolve_declarations();
  void resolve_impls();
  void resolve_structures();
  SemanticTypePtr primitive_type(SemanticType::Kind kind);
  SemanticTypePtr compound(SemanticType::Kind kind, SemanticTypePtr element,
                           bool mutable_ref = false);
  SemanticTypePtr structure_type(const std::string &name);
  SemanticTypePtr resolve_type(const ast::TypePtr &syntax,
                               std::string_view self_name = {});
  SemanticTypePtr resolve_type(const std::optional<ast::TypePtr> &syntax,
                               std::string_view self_name = {});
  static std::vector<const ast::TypePtr *>
  type_arguments(const std::optional<ast::GenericArguments> &arguments);
  static std::vector<const ast::TypePtr *>
  type_arguments(const ast::Path &path);
  SemanticTypePtr resolve_type_impl(const ast::TypePtr &syntax,
                                    std::string_view self_name);
  SemanticTypePtr resolve_path_type(const ast::Path &path, Span span,
                                    std::string_view self_name);

  // Constant evaluation.
  std::optional<std::uint64_t> evaluate_length(const ast::ExprPtr &expression,
                                               std::string_view self_name = {});
  void resolve_constant_type(ConstantInfo &constant);
  void validate_constants();
  void evaluate_constant(ConstantInfo &constant);
  EvaluatedConstant constant_path(const ast::Expr &node,
                                  const ast::PathExpr &path,
                                  std::string_view self_name);
  EvaluatedConstant constant_value(const ast::ExprPtr &expression,
                                   SemanticTypePtr expected = {},
                                   std::string_view self_name = {});
  EvaluatedConstant invalid_constant(const ast::Expr &node);

  // Struct declarations, layout cycles and derives.
  void validate_layouts_and_derives();
  bool validate_layout(const std::string &name);
  bool supports(const SemanticTypePtr &value, const std::string &trait,
                const std::string &root, std::unordered_set<std::string> seen);

  // Function signatures, bodies and scope context.
  std::string_view current_owner() const;
  void resolve_function(FunctionInfo &function, const std::string &,
                        std::string owner);
  void validate_entry();
  void check_functions();
  void check_function(const FunctionInfo &function);
  Binding *lookup(const std::string &name);

  // Coercions and diagnostics.
  void report(Span span, std::string message);
  bool coercible(const SemanticTypePtr &from, const SemanticTypePtr &to);
  SemanticTypePtr common_type(const SemanticTypePtr &first,
                              const SemanticTypePtr &second);
  bool writable(const ExprInfo &value) const;
  void mismatch(Span span, const SemanticTypePtr &actual,
                const SemanticTypePtr &expected);
  std::vector<Adjustment> coercion_adjustments(const SemanticTypePtr &from,
                                               const SemanticTypePtr &to) const;

  // Expression facts and dispatch.
  SemanticTypePtr literal_type(const ast::IntegerLiteral &literal,
                               const SemanticTypePtr &expected, Span span);
  void append_adjustments(ast::NodeId node,
                          std::vector<Adjustment> adjustments);
  void record_node(const ast::Node &node, const ExprInfo &result);
  ExprInfo check_expr(const ast::ExprPtr &expression,
                      SemanticTypePtr expected = {});
  ExprInfo check_expr(const ast::Expr &expression,
                      SemanticTypePtr expected = {},
                      bool record_coercion = true);
  ExprInfo check_path(const ast::Expr &node, const ast::PathExpr &value);
  ControlFlow flow_of(ast::NodeId node) const;
  ControlFlow expression_flow(const ast::Expr &expression) const;
  ControlFlow branch_flow(const ast::IfExpr &branch) const;

  // Aggregate values and projections.
  ExprInfo check_array(const ast::Expr &node, const ast::ArrayExpr &array,
                       const SemanticTypePtr &expected);
  ExprInfo check_repeat_array(const ast::Expr &node,
                              const ast::RepeatArrayExpr &array,
                              const SemanticTypePtr &expected);
  ExprInfo check_struct(const ast::Expr &node,
                        const ast::StructExpr &structure);
  ExprInfo check_field(const ast::Expr &node, const ast::FieldExpr &projection);
  ExprInfo check_index(const ast::Expr &node, const ast::IndexExpr &projection);

  // Blocks and control flow.
  ExprInfo check_block(const ast::Block &block,
                       const SemanticTypePtr &expected = {});
  ExprInfo check_if(const ast::Node &node, const ast::IfExpr &branch,
                    const SemanticTypePtr &expected);
  ExprInfo check_loop(const ast::Expr &node, const ast::LoopExpr &loop_expr,
                      const SemanticTypePtr &expected);
  ExprInfo check_while(const ast::Expr &node, const ast::WhileExpr &while_expr);
  ExprInfo check_break(const ast::Expr &node, const ast::BreakExpr &payload);
  ExprInfo check_continue(const ast::Expr &node, const ast::ContinueExpr &);
  ExprInfo check_return(const ast::Expr &node, const ast::ReturnExpr &payload);

  // Operators and assignment.
  ExprInfo check_borrow(const ast::Expr &node, const ast::BorrowExpr &borrow);
  ExprInfo check_unary(const ast::Expr &node, const ast::UnaryExpr &unary,
                       const SemanticTypePtr &expected);
  OperandSemantics operand_use(const ast::Expr &node, const ExprInfo &info,
                               bool references = true, unsigned depth_limit = 1,
                               bool mutable_references = false);
  ExprInfo check_binary(const ast::Expr &node, const ast::BinaryExpr &binary);
  ExprInfo check_assignment(const ast::Expr &node,
                            const ast::AssignExpr &assignment);
  void check_assignment_target(const ast::ExprPtr &target,
                               const ExprInfo &destination);
  ExprInfo check_compound_assignment(const ast::Expr &node,
                                     const ast::CompoundAssignExpr &assignment);
  ExprInfo check_cast(const ast::Expr &node, const ast::CastExpr &cast);

  // Calls and builtin paths.
  static BuiltinId io_builtin(std::string_view name);
  SemanticTypePtr builtin_path_type(const ast::Path &path,
                                    const std::string &name);
  static bool builtin_function(std::string_view name);
  bool constructor_path(const ast::Path &path);
  ExprInfo check_call(const ast::Expr &node, const ast::CallExpr &call);
  ExprInfo check_builtin_call(const ast::Expr &node,
                              const std::vector<ast::ExprPtr> &arguments,
                              const BuiltinFunctionType &builtin);
  ExprInfo check_signature(const ast::Node &node,
                           const std::vector<ast::ExprPtr> &arguments,
                           const std::vector<SemanticTypePtr> &parameters,
                           SemanticTypePtr result);
  ExprInfo check_method(const ast::Expr &node,
                        const ast::MethodCallExpr &method);
  ExprInfo check_method_signature(
      const ast::Node &node, const std::vector<ast::ExprPtr> &arguments,
      const std::vector<SemanticTypePtr> &parameters, SemanticTypePtr result);

  struct LoopInfo {
    SemanticTypePtr expected;
    std::vector<SemanticTypePtr> break_types;
  };

  const ast::Crate &crate_;
  DiagnosticEngine &diagnostics_;
  std::unordered_map<std::string, StructInfo> structures_;
  std::unordered_map<std::string, FunctionInfo> functions_;
  std::unordered_map<std::string, ConstantInfo> constants_;
  std::vector<const ast::Impl *> impls_;
  std::unordered_map<std::string, FunctionInfo> associated_functions_;
  std::unordered_map<std::string, ConstantInfo> associated_constants_;
  std::map<SymbolId, const FunctionInfo *> callable_functions_;
  std::vector<std::unordered_map<std::string, Binding>> scopes_;
  std::vector<LoopInfo> loops_;
  const FunctionInfo *current_function_{};
  TypeContext types_;
  SemanticData result_;
  SymbolId next_symbol_{1};
  std::map<ast::NodeId, SymbolId> declaration_symbols_;
  std::unordered_set<SymbolId> recorded_symbols_;
  std::map<ast::NodeId, ExpressionSemantics> expression_facts_;
  std::map<ast::NodeId, ControlFlow> control_flows_;
  std::map<ast::NodeId, SemanticTypePtr> resolved_types_;
  std::map<SymbolId, SemanticTypePtr> symbol_types_;
};

} // namespace rx::semantic_detail
