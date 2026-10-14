#include "rx/semantic/analyzer.hpp"
#include <iterator>
#include <limits>

namespace rx::semantic_detail {

Binding *Analyzer::lookup(const std::string &name) {
  for (auto scope = scopes_.rbegin(); scope != scopes_.rend(); ++scope)
    if (auto found = scope->find(name); found != scope->end())
      return &found->second;
  return nullptr;
}

SemanticTypePtr Analyzer::literal_type(const ast::IntegerLiteral &literal,
                                       const SemanticTypePtr &expected,
                                       Span span) {
  SemanticTypePtr result;
  if (literal.suffix) {
    switch (*literal.suffix) {
    case ast::IntegerSuffix::I32:
      result = primitive_type(SemanticType::Kind::I32);
      break;
    case ast::IntegerSuffix::U32:
      result = primitive_type(SemanticType::Kind::U32);
      break;
    case ast::IntegerSuffix::Isize:
      result = primitive_type(SemanticType::Kind::Isize);
      break;
    case ast::IntegerSuffix::Usize:
      result = primitive_type(SemanticType::Kind::Usize);
      break;
    }
  } else if (integer(expected))
    result = expected;
  else
    result = primitive_type(SemanticType::Kind::I32);
  auto value = integer_magnitude(literal);
  if (!value || *value > std::numeric_limits<std::uint32_t>::max())
    report(span, "integer literal is outside the 32-bit target range");
  return result;
}

void Analyzer::append_adjustments(ast::NodeId node,
                                  std::vector<Adjustment> adjustments) {
  auto &fact = expression_facts_[node];
  fact.node = node;
  fact.adjustments.insert(fact.adjustments.end(),
                          std::make_move_iterator(adjustments.begin()),
                          std::make_move_iterator(adjustments.end()));
}

void Analyzer::record_node(const ast::Node &node, const ExprInfo &result) {
  ExpressionSemantics fact;
  fact.node = node.id;
  fact.type = result.type;
  fact.is_place = result.is_place;
  fact.is_mutable = result.is_mutable;
  fact.access_locked = result.access_locked;
  fact.symbol = result.symbol;
  fact.target = result.target;
  fact.adjustments = result.adjustments;
  fact.callable = result.callable;
  fact.binary = result.binary;
  fact.compound = result.compound;
  fact.unary = result.unary;
  fact.receiver = result.receiver;
  expression_facts_[node.id] = std::move(fact);
}

ExprInfo Analyzer::check_expr(const ast::ExprPtr &expression,
                              SemanticTypePtr expected) {
  return check_expr(*expression, std::move(expected));
}

// discuss of exprs.
ExprInfo Analyzer::check_expr(const ast::Expr &expression,
                              SemanticTypePtr expected, bool record_coercion) {
  auto result = ast::visit(
      expression,
      ast::Overloaded{
          [&](const ast::Expr &node,
              const ast::IntegerExpr &payload) -> ExprInfo {
            return {literal_type(payload.literal, expected, node.span)};
          },
          [&](const ast::Expr &, const ast::BooleanExpr &) -> ExprInfo {
            return {primitive_type(SemanticType::Kind::Bool)};
          },
          [&](const ast::Expr &, const ast::UnitExpr &) -> ExprInfo {
            return {primitive_type(SemanticType::Kind::Unit)};
          },
          [&](const ast::Expr &, const ast::ParenExpr &payload) {
            auto inner = check_expr(*payload.inner, expected, false);
            inner.adjustments.clear();
            inner.binary.reset();
            inner.compound.reset();
            inner.unary.reset();
            inner.receiver.reset();
            return inner;
          },
          [&](const ast::Expr &node, const ast::PathExpr &path) {
            return check_path(node, path);
          },
          [&](const ast::Expr &node, const ast::ArrayExpr &array) {
            return check_array(node, array, expected);
          },
          [&](const ast::Expr &node, const ast::RepeatArrayExpr &array) {
            return check_repeat_array(node, array, expected);
          },
          [&](const ast::Expr &node, const ast::StructExpr &structure) {
            return check_struct(node, structure);
          },
          [&](const ast::Expr &, const ast::BlockExpr &payload) {
            return check_block(*payload.block, expected);
          },
          [&](const ast::Expr &node, const ast::IfExpr &payload) {
            return check_if(node, payload, expected);
          },
          [&](const ast::Expr &node, const ast::LoopExpr &loop_expr) {
            return check_loop(node, loop_expr, expected);
          },
          [&](const ast::Expr &node, const ast::WhileExpr &while_expr) {
            return check_while(node, while_expr);
          },
          [&](const ast::Expr &node, const ast::UnaryExpr &unary) {
            return check_unary(node, unary, expected);
          },
          [&](const ast::Expr &node, const ast::BorrowExpr &borrow) {
            return check_borrow(node, borrow);
          },
          [&](const ast::Expr &node, const ast::BinaryExpr &binary) {
            return check_binary(node, binary);
          },
          [&](const ast::Expr &node, const ast::AssignExpr &assignment) {
            return check_assignment(node, assignment);
          },
          [&](const ast::Expr &node,
              const ast::CompoundAssignExpr &assignment) {
            return check_compound_assignment(node, assignment);
          },
          [&](const ast::Expr &node, const ast::CastExpr &cast) {
            return check_cast(node, cast);
          },
          [&](const ast::Expr &node, const ast::CallExpr &call) {
            return check_call(node, call);
          },
          [&](const ast::Expr &node, const ast::MethodCallExpr &method) {
            return check_method(node, method);
          },
          [&](const ast::Expr &node, const ast::FieldExpr &projection) {
            return check_field(node, projection);
          },
          [&](const ast::Expr &node, const ast::IndexExpr &projection) {
            return check_index(node, projection);
          },
          [&](const ast::Expr &node, const ast::BreakExpr &payload) {
            return check_break(node, payload);
          },
          [&](const ast::Expr &node, const ast::ContinueExpr &payload) {
            return check_continue(node, payload);
          },
          [&](const ast::Expr &node, const ast::ReturnExpr &payload) {
            return check_return(node, payload);
          }});
  if (expected && record_coercion && coercible(result.type, expected)) {
    auto adjustments = coercion_adjustments(result.type, expected);
    result.adjustments.insert(result.adjustments.end(), adjustments.begin(),
                              adjustments.end());
  }
  record_node(expression, result);
  return result;
}

ExprInfo Analyzer::check_path(const ast::Expr &node,
                              const ast::PathExpr &value) {
  const auto &path = value.path;
  auto name = ast::path_name(path);
  if (!type_arguments(path).empty() && !constructor_path(path))
    report(node.span, "functions have no type parameters");
  if (path.segments.size() > 1 &&
      std::string(ast::name_text(path.segments.front().name)) == "Self" &&
      current_function_ && !current_function_->owner.empty()) {
    name = current_function_->owner;
    for (std::size_t index = 1; index < path.segments.size(); ++index)
      name += "::" + std::string(ast::name_text(path.segments[index].name));
  } // deconstruct the self

  if (path.segments.size() == 1) {
    if (auto *binding = lookup(name))
      return {binding->type,   true, binding->is_mutable, false,
              binding->symbol, name};
    if (auto found = constants_.find(name); found != constants_.end())
      return {found->second.type,
              false,
              false,
              false,
              symbol_for(found->second.ast->id),
              name};
    if (auto found = functions_.find(name); found != functions_.end()) {
      auto value = types_.function(symbol_for(found->second.ast->id), name);
      return {value, false, false, false, symbol_for(found->second.ast->id),
              name};
    }
    if (builtin_function(name)) {
      auto value = types_.builtin(io_builtin(name));
      return {value, false, false, false, std::nullopt, "builtin::" + name};
    }

  } else {
    if (auto found = associated_constants_.find(name);
        found != associated_constants_.end())
      return {found->second.type,
              false,
              false,
              false,
              symbol_for(found->second.ast->id),
              name};
    if (auto found = associated_functions_.find(name);
        found != associated_functions_.end()) {
      auto value = types_.function(symbol_for(found->second.ast->id), name);
      return {value, false, false, false, symbol_for(found->second.ast->id),
              name};
    }
    if (constructor_path(path)) {
      auto value = builtin_path_type(path, name);
      std::optional<SymbolId> symbol;
      if (auto found = structures_.find(
              std::string(ast::name_text(path.segments.front().name)));
          found != structures_.end())
        symbol = symbol_for(found->second.ast->id);
      return {value, false, false, false, symbol, "builtin::" + name};
    }
  }
  report(node.span, "unresolved value name '" + name + "'");
  return {};
}

} // namespace rx::semantic_detail
