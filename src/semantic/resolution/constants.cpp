#include "semantic/analyzer.hpp"

#include <limits>

namespace rx::semantic_detail {

std::optional<std::uint64_t>
Analyzer::evaluate_length(const ast::ExprPtr &expression,
                          std::string_view self_name) {
  auto errors = diagnostics_.error_count();
  auto [type, value] = constant_value(
      expression, primitive_type(SemanticType::Kind::Usize), self_name);
  if (type->kind() != SemanticType::Kind::Usize)
    report(expression->span, "array length must have type usize");
  if (value < 0)
    report(expression->span, "array length cannot be negative");
  if (diagnostics_.error_count() != errors)
    return {};
  return static_cast<std::uint64_t>(value);
}

void Analyzer::resolve_constant_type(ConstantInfo &constant) {
  if (constant.type_state == ConstantInfo::TypeState::Done ||
      constant.type_state == ConstantInfo::TypeState::Failed)
    return;
  if (constant.type_state == ConstantInfo::TypeState::Resolving) {
    report(constant.ast->span, "constant type dependency cycle");
    constant.type = primitive_type(SemanticType::Kind::Error);
    constant.type_state = ConstantInfo::TypeState::Failed;
    return;
  }
  constant.type_state = ConstantInfo::TypeState::Resolving;
  constant.type = resolve_type(constant.ast->type, constant.owner);
  record_symbol_type(constant.ast->id, constant.type);
  if (constant.type_state != ConstantInfo::TypeState::Failed)
    constant.type_state = ConstantInfo::TypeState::Done;
}

void Analyzer::validate_constants() {
  for (auto &[_, constant] : constants_)
    evaluate_constant(constant);
  for (auto &[_, constant] : associated_constants_)
    evaluate_constant(constant);
}

void Analyzer::evaluate_constant(ConstantInfo &constant) {

  resolve_constant_type(constant);
  if (constant.state == ConstantInfo::State::Done ||
      constant.state == ConstantInfo::State::Failed)
    return;
  if (constant.state == ConstantInfo::State::Evaluating) {
    report(constant.ast->span, "constant dependency cycle");
    constant.state = ConstantInfo::State::Failed;
    return;
  }
  constant.state = ConstantInfo::State::Evaluating;
  auto [value_type, value] =
      constant_value(constant.ast->value, constant.type, constant.owner);
  if (!compatible_types(constant.type, value_type)) {
    report(constant.ast->span, "constant initializer has type " +
                                   type_name(value_type) + ", expected " +
                                   type_name(constant.type));
    constant.state = ConstantInfo::State::Failed;
    return;
  }
  constant.value = value;
  constant.state = ConstantInfo::State::Done;
}

Analyzer::EvaluatedConstant
Analyzer::constant_path(const ast::Expr &node, const ast::PathExpr &path,
                        std::string_view self_name) {
  auto name = ast::path_name(path.path);
  if (!self_name.empty() && name.compare(0, 6, "Self::") == 0)
    name = std::string(self_name) + name.substr(4);
  if (auto found = constants_.find(name); found != constants_.end()) {
    evaluate_constant(found->second);
    record_node(node, ExprInfo{found->second.type, false, false, false,
                               symbol_for(found->second.ast->id), name});
    return {found->second.type, found->second.value};
  }
  if (auto found = associated_constants_.find(name);
      found != associated_constants_.end()) {
    evaluate_constant(found->second);
    record_node(node, ExprInfo{found->second.type, false, false, false,
                               symbol_for(found->second.ast->id), name});
    return {found->second.type, found->second.value};
  }
  report(node.span, "constant path does not resolve to a constant");
  auto error = primitive_type(SemanticType::Kind::Error);
  record_node(node, ExprInfo{error});
  return {error, 0};
}

Analyzer::EvaluatedConstant
Analyzer::constant_value(const ast::ExprPtr &expression,
                         SemanticTypePtr expected, std::string_view self_name) {
  return ast::visit(
      *expression,
      ast::Overloaded{
          [&](const ast::Expr &node,
              const ast::ParenExpr &payload) -> EvaluatedConstant {
            auto value = constant_value(payload.inner, expected, self_name);
            record_node(node, ExprInfo{value.first});
            return value;
          },
          [&](const ast::Expr &node,
              const ast::BooleanExpr &payload) -> EvaluatedConstant {
            auto type = primitive_type(SemanticType::Kind::Bool);
            record_node(node, ExprInfo{type});
            return {type, payload.value};
          },
          [&](const ast::Expr &node,
              const ast::IntegerExpr &payload) -> EvaluatedConstant {
            auto type = literal_type(payload.literal, expected, node.span);
            auto magnitude = integer_magnitude(payload.literal).value_or(0);

            if (magnitude > std::numeric_limits<std::uint32_t>::max())
              magnitude = 0;
            record_node(node, ExprInfo{type});
            return {type, static_cast<std::int64_t>(magnitude)};
          },
          [&](const ast::Expr &node,
              const ast::UnaryExpr &payload) -> EvaluatedConstant {
            if (payload.op != ast::UnaryOp::Negate)
              return invalid_constant(node);
            auto [type, value] =
                constant_value(payload.operand, expected, self_name);
            if (!signed_integer(type))
              report(node.span, "only signed constants may be negated");
            ExprInfo result{type};
            result.unary = ResolvedUnary{
                UnaryOperation::Negate,
                OperandSemantics{payload.operand->id, type, {}}, type};
            record_node(node, result);
            return {type, -value};
          },
          [&](const ast::Expr &node,
              const ast::PathExpr &payload) -> EvaluatedConstant {
            return constant_path(node, payload, self_name);
          },
          [&](const ast::Expr &node, const auto &) -> EvaluatedConstant {
            return invalid_constant(node);
          }});
}

Analyzer::EvaluatedConstant Analyzer::invalid_constant(const ast::Expr &node) {
  report(node.span, "invalid constant expression");
  auto error = primitive_type(SemanticType::Kind::Error);
  record_node(node, ExprInfo{error});
  return {error, 0};
}

} // namespace rx::semantic_detail
