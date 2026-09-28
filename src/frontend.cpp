#include "rx/frontend.hpp"

#include "RxLexer.h"
#include "RxParser.h"
#include "antlr4-runtime.h"

#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace rx {
namespace {

using ParseContext = antlr4::ParserRuleContext;
using ParseTree = antlr4::tree::ParseTree;
using TerminalNode = antlr4::tree::TerminalNode;

class SyntaxListener final : public antlr4::BaseErrorListener {
public:
  explicit SyntaxListener(DiagnosticEngine &diagnostics)
      : diagnostics_(diagnostics) {}

  void syntaxError(antlr4::Recognizer *, antlr4::Token *offending,
                   std::size_t line, std::size_t column,
                   const std::string &message, std::exception_ptr) override {
    Span span{0, 0, line, column};
    if (offending) {
      span.begin = offending->getStartIndex();
      span.end = offending->getStopIndex() + 1;
    }
    diagnostics_.error(span, message);
  }

private:
  DiagnosticEngine &diagnostics_;
};

Span source_span(ParseContext *context) {
  const auto *start = context->getStart();
  const auto *stop = context->getStop();
  if (!start)
    return {};
  const auto begin = start->getStartIndex();
  const auto end = stop && stop->getType() != antlr4::Token::EOF
                       ? stop->getStopIndex() + 1
                       : begin;
  return {begin, end, start->getLine(), start->getCharPositionInLine()};
}

Span merge(Span first, Span second) {
  first.end = second.end;
  return first;
}

// AstBuilder consumes only generated parser contexts; it never reparses tokens.
class AstBuilder {
public:
  explicit AstBuilder(const std::vector<std::string> &rule_names)
      : rule_names_(rule_names) {}

  std::shared_ptr<ast::Crate>
  build(rxgrammar::RxParser::CrateContext *context) {
    auto crate = std::make_shared<ast::Crate>();
    stamp(*crate, context);
    for (auto *item : context->item()) {
      if (item->useDeclaration())
        crate->items.emplace_back(build_use(item->useDeclaration()));
      else if (item->functionDefinition())
        crate->items.emplace_back(build_function(item->functionDefinition()));
      else if (item->structDefinition())
        crate->items.emplace_back(build_struct(item->structDefinition()));
      else if (item->constantItem())
        crate->items.emplace_back(build_constant(item->constantItem()));
      else if (item->inherentImpl())
        crate->items.emplace_back(build_impl(item->inherentImpl()));
    }
    if (!crate->items.empty()) {
      std::visit([&](const auto &item) { crate->span = item.span; },
                 crate->items.front());
      std::visit([&](const auto &item) { crate->span.end = item.span.end; },
                 crate->items.back());
    }
    return crate;
  }

private:
  template <typename Node> void stamp(Node &node, ParseContext *context) {
    node.id = next_id_++;
    node.span = source_span(context);
  }

  std::string_view rule(ParseContext *context) const {
    return rule_names_.at(context->getRuleIndex());
  }

  static ParseContext *as_context(ParseTree *tree) {
    return dynamic_cast<ParseContext *>(tree);
  }

  static TerminalNode *as_terminal(ParseTree *tree) {
    return dynamic_cast<TerminalNode *>(tree);
  }

  // One builder handles the ordinary, condition, and statement rule families.
  bool is_expression_context(ParseContext *context) const {
    const auto name = rule(context);
    return name == "expression" || name.ends_with("Expression") ||
           name == "nonBlockPrimary" || name == "conditionPrimary" ||
           name == "conditionPrimaryWithoutBareBlock";
  }

  static bool is_operator_rule(std::string_view name) {
    return name == "assignmentOperator" || name == "equalsSign" ||
           name == "comparisonExceptLt" || name == "shiftRight" ||
           name == "additiveOperator" || name == "multiplicativeOperator" ||
           name == "genericClose";
  }

  static bool is_binary_rule(std::string_view name) {
    return name.ends_with("Expression") &&
           (name.contains("LogicalOr") || name.starts_with("logicalOr") ||
            name.contains("LogicalAnd") || name.starts_with("logicalAnd") ||
            name.contains("Comparison") || name.starts_with("comparison") ||
            name.contains("BitOr") || name.starts_with("bitOr") ||
            name.contains("BitXor") || name.starts_with("bitXor") ||
            name.contains("BitAnd") || name.starts_with("bitAnd") ||
            name.contains("Shift") || name.starts_with("shift") ||
            name.contains("Additive") || name.starts_with("additive") ||
            name.contains("Multiplicative") ||
            name.starts_with("multiplicative"));
  }

  ast::ExprPtr make_expr(ast::Expr::Kind kind, ParseContext *context,
                         std::string text = {}) {
    auto expression = std::make_shared<ast::Expr>();
    stamp(*expression, context);
    expression->kind = kind;
    expression->text = std::move(text);
    return expression;
  }

  ast::TypePtr unit_type(ParseContext *context) {
    auto type = std::make_shared<ast::Type>();
    stamp(*type, context);
    type->kind = ast::Type::Kind::Unit;
    return type;
  }

  std::vector<std::string>
  lifetime_parameters(rxgrammar::RxParser::GenericParamsContext *context) {
    std::vector<std::string> result;
    if (!context)
      return result;
    for (auto *parameter : context->lifetimeParam())
      result.push_back(parameter->lifetime()->getText());
    return result;
  }

  void
  append_generic_arguments(std::vector<ast::TypePtr> &arguments,
                           rxgrammar::RxParser::GenericArgsContext *context) {
    if (!context)
      return;
    for (auto *argument : context->genericArg())
      if (argument->typeRef())
        arguments.push_back(build_type(argument->typeRef()));
  }

  void
  append_type_segment(std::vector<std::string> &path,
                      std::vector<ast::TypePtr> &arguments,
                      rxgrammar::RxParser::TypePathSegmentContext *segment) {
    path.push_back(segment->pathIdentSegment()->getText());
    append_generic_arguments(arguments, segment->genericArgs());
  }

  void append_expression_segment(
      std::vector<std::string> &path, std::vector<ast::TypePtr> &arguments,
      rxgrammar::RxParser::PathExprSegmentContext *segment) {
    path.push_back(segment->pathIdentSegment()->getText());
    append_generic_arguments(arguments, segment->genericArgs());
  }

  ast::TypePtr build_type(ParseContext *context) {
    if (auto *type =
            dynamic_cast<rxgrammar::RxParser::TypeRefContext *>(context)) {
      if (type->typeRef())
        return build_type(type->typeRef());
      if (type->typePath()) {
        auto result = std::make_shared<ast::Type>();
        stamp(*result, type);
        result->kind = ast::Type::Kind::Path;
        for (auto *segment : type->typePath()->typePathSegment())
          append_type_segment(result->path, result->arguments, segment);
        return result;
      }
      if (type->referenceType())
        return build_type(type->referenceType());
      if (type->arrayType())
        return build_type(type->arrayType());
      return unit_type(type);
    }
    if (auto *reference =
            dynamic_cast<rxgrammar::RxParser::ReferenceTypeContext *>(
                context)) {
      auto inner = std::make_shared<ast::Type>();
      stamp(*inner, reference);
      inner->kind = ast::Type::Kind::Reference;
      inner->is_mutable = reference->MUT() != nullptr;
      inner->element = build_type(reference->typeRef());
      if (!reference->ANDAND())
        return inner;
      auto outer = std::make_shared<ast::Type>();
      stamp(*outer, reference);
      outer->kind = ast::Type::Kind::Reference;
      outer->element = std::move(inner);
      return outer;
    }
    if (auto *array =
            dynamic_cast<rxgrammar::RxParser::ArrayTypeContext *>(context)) {
      auto result = std::make_shared<ast::Type>();
      stamp(*result, array);
      result->kind = ast::Type::Kind::Array;
      result->element = build_type(array->typeRef());
      result->array_length = array->constValue()->getText();
      return result;
    }
    auto *closed =
        dynamic_cast<rxgrammar::RxParser::ClosedCastTypeContext *>(context);
    if (!closed)
      throw std::logic_error("unexpected type parse context");
    if (closed->typeRef())
      return build_type(closed->typeRef());
    if (closed->arrayType())
      return build_type(closed->arrayType());
    if (closed->closedCastType()) {
      auto inner = std::make_shared<ast::Type>();
      stamp(*inner, closed);
      inner->kind = ast::Type::Kind::Reference;
      inner->is_mutable = closed->MUT() != nullptr;
      inner->element = build_type(closed->closedCastType());
      if (!closed->ANDAND())
        return inner;
      auto outer = std::make_shared<ast::Type>();
      stamp(*outer, closed);
      outer->kind = ast::Type::Kind::Reference;
      outer->element = std::move(inner);
      return outer;
    }
    if (closed->pathIdentSegment()) {
      auto result = std::make_shared<ast::Type>();
      stamp(*result, closed);
      result->kind = ast::Type::Kind::Path;
      for (auto *segment : closed->typePathSegment())
        append_type_segment(result->path, result->arguments, segment);
      result->path.push_back(closed->pathIdentSegment()->getText());
      append_generic_arguments(result->arguments, closed->genericArgs());
      return result;
    }
    return unit_type(closed);
  }

  ast::ExprPtr
  build_path(rxgrammar::RxParser::PathInExpressionContext *context) {
    auto result = make_expr(ast::Expr::Kind::Path, context);
    for (auto *segment : context->pathExprSegment())
      append_expression_segment(result->path, result->type_arguments, segment);
    return result;
  }

  ast::ExprPtr build_magnitude(rxgrammar::RxParser::MagnitudeContext *context) {
    if (context->INTEGER_LITERAL())
      return make_expr(ast::Expr::Kind::Integer, context, context->getText());
    if (context->pathInExpression())
      return build_path(context->pathInExpression());
    return build_magnitude(context->magnitude());
  }

  ast::ExprPtr build_constant(rxgrammar::RxParser::ConstValueContext *context) {
    if (context->INTEGER_LITERAL())
      return make_expr(ast::Expr::Kind::Integer, context, context->getText());
    if (context->TRUE() || context->FALSE())
      return make_expr(ast::Expr::Kind::Boolean, context, context->getText());
    if (context->pathInExpression())
      return build_path(context->pathInExpression());
    if (context->MINUS()) {
      auto result = make_expr(ast::Expr::Kind::Unary, context, "-");
      result->operands.push_back(build_magnitude(context->magnitude()));
      return result;
    }
    return build_constant(context->constValue());
  }

  ast::ExprPtr
  build_literal(rxgrammar::RxParser::LiteralExpressionContext *context) {
    return make_expr(context->INTEGER_LITERAL() ? ast::Expr::Kind::Integer
                                                : ast::Expr::Kind::Boolean,
                     context, context->getText());
  }

  ast::ExprPtr
  build_array(rxgrammar::RxParser::ArrayExpressionContext *context) {
    auto result = make_expr(ast::Expr::Kind::Array, context);
    for (auto *element : context->expression())
      result->operands.push_back(build_expression(element));
    if (context->SEMI()) {
      result->flag = true;
      result->operands.push_back(build_constant(context->constValue()));
    }
    return result;
  }

  ast::ExprPtr build_if(rxgrammar::RxParser::IfExpressionContext *context) {
    auto result = make_expr(ast::Expr::Kind::If, context);
    result->operands.push_back(
        build_expression(context->conditionExpression()));
    result->operands.push_back(build_block(context->blockExpression(0)));
    if (context->ifExpression())
      result->operands.push_back(build_if(context->ifExpression()));
    else if (context->blockExpression().size() > 1)
      result->operands.push_back(build_block(context->blockExpression(1)));
    return result;
  }

  ast::ExprPtr build_expression_with_block(
      rxgrammar::RxParser::ExpressionWithBlockContext *context) {
    if (context->blockExpression() && !context->LOOP() && !context->WHILE())
      return build_block(context->blockExpression());
    if (context->ifExpression())
      return build_if(context->ifExpression());
    if (context->LOOP()) {
      auto result = make_expr(ast::Expr::Kind::Loop, context);
      result->operands.push_back(build_block(context->blockExpression()));
      return result;
    }
    auto result = make_expr(ast::Expr::Kind::While, context);
    result->operands.push_back(
        build_expression(context->conditionExpression()));
    result->operands.push_back(build_block(context->blockExpression()));
    return result;
  }

  std::shared_ptr<ast::Stmt>
  build_let(rxgrammar::RxParser::LetStatementContext *context) {
    auto statement = std::make_shared<ast::Stmt>();
    stamp(*statement, context);
    statement->kind = ast::Stmt::Kind::Let;
    statement->name = context->identifierBinding()->identifier()->getText();
    statement->is_mutable = context->identifierBinding()->MUT() != nullptr;
    if (context->typeRef())
      statement->annotation = build_type(context->typeRef());
    statement->expression = build_expression(context->expression());
    statement->has_semicolon = true;
    return statement;
  }

  std::shared_ptr<ast::Stmt>
  build_statement(rxgrammar::RxParser::StatementContext *context) {
    if (context->letStatement())
      return build_let(context->letStatement());
    auto statement = std::make_shared<ast::Stmt>();
    stamp(*statement, context);
    if (!context->expressionWithBlock() && !context->statementExpression()) {
      statement->kind = ast::Stmt::Kind::Empty;
      statement->has_semicolon = true;
      return statement;
    }
    statement->kind = ast::Stmt::Kind::Expr;
    statement->expression =
        context->expressionWithBlock()
            ? build_expression_with_block(context->expressionWithBlock())
            : build_expression(context->statementExpression());
    statement->has_semicolon = context->SEMI() != nullptr;
    return statement;
  }

  ast::ExprPtr
  build_block(rxgrammar::RxParser::BlockExpressionContext *context) {
    auto block = make_expr(ast::Expr::Kind::Block, context);
    for (auto *child : context->statement())
      block->statements.push_back(build_statement(child));
    if (context->statementExpression()) {
      auto tail = std::make_shared<ast::Stmt>();
      stamp(*tail, context->statementExpression());
      tail->kind = ast::Stmt::Kind::Expr;
      tail->expression = build_expression(context->statementExpression());
      block->statements.push_back(std::move(tail));
    }
    return block;
  }

  ast::ExprPtr build_primary(ParseContext *context) {
    if (auto *primary =
            dynamic_cast<rxgrammar::RxParser::PrimaryExpressionContext *>(
                context)) {
      if (primary->nonBlockPrimary())
        return build_primary(primary->nonBlockPrimary());
      return build_expression_with_block(primary->expressionWithBlock());
    }
    if (auto *primary =
            dynamic_cast<rxgrammar::RxParser::ConditionPrimaryContext *>(
                context)) {
      if (primary->conditionPrimaryWithoutBareBlock())
        return build_primary(primary->conditionPrimaryWithoutBareBlock());
      return build_block(primary->blockExpression());
    }
    if (auto *primary =
            dynamic_cast<rxgrammar::RxParser::NonBlockPrimaryContext *>(
                context)) {
      if (primary->literalExpression())
        return build_literal(primary->literalExpression());
      if (primary->pathInExpression()) {
        auto result = build_path(primary->pathInExpression());
        if (!primary->LBRACE())
          return result;
        result->kind = ast::Expr::Kind::Struct;
        result->span = source_span(primary);
        if (primary->structExprFields())
          for (auto *field : primary->structExprFields()->structExprField())
            result->fields.push_back({field->identifier()->getText(),
                                      build_expression(field->expression()),
                                      source_span(field)});
        return result;
      }
      if (primary->arrayExpression())
        return build_array(primary->arrayExpression());
      if (primary->BREAK() || primary->RETURN()) {
        auto result = make_expr(primary->BREAK() ? ast::Expr::Kind::Break
                                                 : ast::Expr::Kind::Return,
                                primary);
        if (primary->expression())
          result->operands.push_back(build_expression(primary->expression()));
        return result;
      }
      if (primary->CONTINUE())
        return make_expr(ast::Expr::Kind::Continue, primary);
      if (primary->expression())
        return build_expression(primary->expression());
      return make_expr(ast::Expr::Kind::Unit, primary);
    }
    auto *primary = dynamic_cast<
        rxgrammar::RxParser::ConditionPrimaryWithoutBareBlockContext *>(
        context);
    if (!primary)
      throw std::logic_error("unexpected primary parse context");
    if (primary->literalExpression())
      return build_literal(primary->literalExpression());
    if (primary->pathInExpression())
      return build_path(primary->pathInExpression());
    if (primary->arrayExpression())
      return build_array(primary->arrayExpression());
    if (primary->ifExpression())
      return build_if(primary->ifExpression());
    if (primary->LOOP()) {
      auto result = make_expr(ast::Expr::Kind::Loop, primary);
      result->operands.push_back(build_block(primary->blockExpression()));
      return result;
    }
    if (primary->WHILE()) {
      auto result = make_expr(ast::Expr::Kind::While, primary);
      result->operands.push_back(
          build_expression(primary->conditionExpression()));
      result->operands.push_back(build_block(primary->blockExpression()));
      return result;
    }
    if (primary->BREAK() || primary->RETURN()) {
      auto result = make_expr(primary->BREAK() ? ast::Expr::Kind::Break
                                               : ast::Expr::Kind::Return,
                              primary);
      if (primary->conditionBreakExpression())
        result->operands.push_back(
            build_expression(primary->conditionBreakExpression()));
      else if (primary->conditionExpression())
        result->operands.push_back(
            build_expression(primary->conditionExpression()));
      return result;
    }
    if (primary->CONTINUE())
      return make_expr(ast::Expr::Kind::Continue, primary);
    if (primary->expression())
      return build_expression(primary->expression());
    return make_expr(ast::Expr::Kind::Unit, primary);
  }

  ast::ExprPtr apply_call(ast::ExprPtr base,
                          rxgrammar::RxParser::CallArgumentsContext *context) {
    auto call = make_expr(ast::Expr::Kind::Call, context);
    call->span = merge(base->span, source_span(context));
    call->operands.push_back(std::move(base));
    for (auto *argument : context->expression())
      call->operands.push_back(build_expression(argument));
    return call;
  }

  ast::ExprPtr apply_dot(ast::ExprPtr base,
                         rxgrammar::RxParser::DotSuffixContext *context) {
    if (!context->callArguments()) {
      auto field = make_expr(ast::Expr::Kind::Field, context,
                             context->identifier()->getText());
      field->span = merge(base->span, source_span(context));
      field->operands.push_back(std::move(base));
      return field;
    }
    auto method =
        make_expr(ast::Expr::Kind::MethodCall, context,
                  context->pathExprSegment()->pathIdentSegment()->getText());
    method->span = merge(base->span, source_span(context));
    append_generic_arguments(method->type_arguments,
                             context->pathExprSegment()->genericArgs());
    method->operands.push_back(std::move(base));
    for (auto *argument : context->callArguments()->expression())
      method->operands.push_back(build_expression(argument));
    return method;
  }

  ast::ExprPtr
  apply_postfix(ast::ExprPtr base,
                rxgrammar::RxParser::PostfixSuffixContext *context) {
    if (context->callArguments())
      return apply_call(std::move(base), context->callArguments());
    if (context->dotSuffix())
      return apply_dot(std::move(base), context->dotSuffix());
    auto index = make_expr(ast::Expr::Kind::Index, context);
    index->span = merge(base->span, source_span(context));
    index->operands.push_back(std::move(base));
    index->operands.push_back(build_expression(context->expression()));
    return index;
  }

  ast::ExprPtr build_postfix(ParseContext *context) {
    ast::ExprPtr result;
    // Context order preserves the source order of a chained postfix expression.
    for (auto *child : context->children) {
      auto *child_context = as_context(child);
      if (!child_context)
        continue;
      const auto name = rule(child_context);
      if (!result) {
        result = build_expression(child_context);
      } else if (name == "postfixSuffix") {
        result = apply_postfix(
            std::move(result),
            static_cast<rxgrammar::RxParser::PostfixSuffixContext *>(
                child_context));
      } else if (name == "dotSuffix") {
        result = apply_dot(std::move(result),
                           static_cast<rxgrammar::RxParser::DotSuffixContext *>(
                               child_context));
      }
    }
    return result;
  }

  ast::ExprPtr build_unary(ParseContext *context) {
    std::string operator_text;
    ast::ExprPtr operand;
    for (auto *child : context->children) {
      auto *child_context = as_context(child);
      if (!child_context)
        continue;
      if (rule(child_context) == "unaryOperator")
        operator_text = child_context->getText();
      else if (is_expression_context(child_context))
        operand = build_expression(child_context);
    }
    if (operator_text.empty())
      return operand;
    auto result =
        make_expr(ast::Expr::Kind::Unary, context, std::move(operator_text));
    result->operands.push_back(std::move(operand));
    return result;
  }

  ast::ExprPtr build_cast(ParseContext *context) {
    ast::ExprPtr result;
    for (auto *child : context->children) {
      auto *child_context = as_context(child);
      if (!child_context)
        continue;
      const auto name = rule(child_context);
      if (is_expression_context(child_context)) {
        result = build_expression(child_context);
      } else if (name == "typeRef" || name == "closedCastType") {
        auto cast = make_expr(ast::Expr::Kind::Cast, context, "as");
        cast->span = merge(result->span, source_span(child_context));
        cast->operands.push_back(std::move(result));
        cast->cast_type = build_type(child_context);
        result = std::move(cast);
      }
    }
    return result;
  }

  ast::ExprPtr fold_binary(ParseContext *context) {
    ast::ExprPtr result;
    std::string operator_text;
    // Precedence is already encoded by the generated parser rule hierarchy.
    for (auto *child : context->children) {
      if (auto *terminal = as_terminal(child)) {
        operator_text = terminal->getText();
        continue;
      }
      auto *child_context = as_context(child);
      if (!child_context)
        continue;
      if (is_operator_rule(rule(child_context))) {
        operator_text = child_context->getText();
        continue;
      }
      if (!is_expression_context(child_context))
        continue;
      auto operand = build_expression(child_context);
      if (!result) {
        result = std::move(operand);
        continue;
      }
      auto binary = make_expr(ast::Expr::Kind::Binary, context, operator_text);
      binary->span = merge(result->span, operand->span);
      binary->operands = {std::move(result), std::move(operand)};
      result = std::move(binary);
    }
    return result;
  }

  ast::ExprPtr build_assignment(ParseContext *context) {
    ast::ExprPtr left;
    ast::ExprPtr right;
    std::string operator_text;
    for (auto *child : context->children) {
      auto *child_context = as_context(child);
      if (!child_context)
        continue;
      if (is_operator_rule(rule(child_context)))
        operator_text = child_context->getText();
      else if (is_expression_context(child_context)) {
        if (!left)
          left = build_expression(child_context);
        else
          right = build_expression(child_context);
      }
    }
    if (!right)
      return left;
    auto assignment =
        make_expr(ast::Expr::Kind::Assign, context, std::move(operator_text));
    assignment->operands = {std::move(left), std::move(right)};
    return assignment;
  }

  ast::ExprPtr build_expression(ParseContext *context) {
    const auto name = rule(context);
    if (name == "blockExpression")
      return build_block(
          static_cast<rxgrammar::RxParser::BlockExpressionContext *>(context));
    if (name == "expressionWithBlock")
      return build_expression_with_block(
          static_cast<rxgrammar::RxParser::ExpressionWithBlockContext *>(
              context));
    if (name == "ifExpression")
      return build_if(
          static_cast<rxgrammar::RxParser::IfExpressionContext *>(context));
    if (name == "literalExpression")
      return build_literal(
          static_cast<rxgrammar::RxParser::LiteralExpressionContext *>(
              context));
    if (name == "arrayExpression")
      return build_array(
          static_cast<rxgrammar::RxParser::ArrayExpressionContext *>(context));
    if (name == "primaryExpression" || name == "nonBlockPrimary" ||
        name == "conditionPrimary" ||
        name == "conditionPrimaryWithoutBareBlock")
      return build_primary(context);
    if (name == "postfixExpression" || name.contains("PostfixExpression"))
      return build_postfix(context);
    if (name == "unaryExpression" || name.contains("UnaryExpression"))
      return build_unary(context);
    if (name == "castExpression" || name.contains("CastExpression"))
      return build_cast(context);
    if (name == "assignmentExpression" || name.contains("AssignmentExpression"))
      return build_assignment(context);
    if (is_binary_rule(name))
      return fold_binary(context);
    for (auto *child : context->children)
      if (auto *child_context = as_context(child);
          child_context && is_expression_context(child_context))
        return build_expression(child_context);
    throw std::logic_error("unsupported expression parse context: " +
                           std::string(name));
  }

  ast::Use build_use(rxgrammar::RxParser::UseDeclarationContext *context) {
    ast::Use use;
    stamp(use, context);
    use.text = context->useTree()->getText();
    return use;
  }

  ast::Parameter
  build_parameter(rxgrammar::RxParser::FunctionParamContext *context) {
    ast::Parameter parameter;
    stamp(parameter, context);
    parameter.name = context->identifierBinding()->identifier()->getText();
    parameter.is_mutable = context->identifierBinding()->MUT() != nullptr;
    parameter.type = build_type(context->typeRef());
    return parameter;
  }

  ast::Function
  build_function(rxgrammar::RxParser::FunctionDefinitionContext *context) {
    ast::Function function;
    stamp(function, context);
    function.name = context->identifier()->getText();
    function.lifetime_parameters =
        lifetime_parameters(context->genericParams());
    if (auto *parameters = context->functionParameters()) {
      if (auto *self = parameters->selfParam()) {
        function.has_self = true;
        function.self_by_ref = self->AMP() != nullptr;
        function.self_mutable = self->MUT() != nullptr;
      }
      for (auto *parameter : parameters->functionParam())
        function.parameters.push_back(build_parameter(parameter));
    }
    function.result = context->typeRef() ? build_type(context->typeRef())
                                         : unit_type(context);
    function.body = build_block(context->blockExpression());
    return function;
  }

  ast::Struct
  build_struct(rxgrammar::RxParser::StructDefinitionContext *context) {
    ast::Struct structure;
    stamp(structure, context);
    structure.name = context->identifier()->getText();
    structure.lifetime_parameters =
        lifetime_parameters(context->genericParams());
    for (auto *attribute : context->outerAttribute())
      for (auto *derive : attribute->deriveName())
        structure.derives.push_back(derive->getText());
    for (auto *field_context : context->structField()) {
      ast::StructField field;
      stamp(field, field_context);
      field.name = field_context->identifier()->getText();
      field.type = build_type(field_context->typeRef());
      structure.fields.push_back(std::move(field));
    }
    return structure;
  }

  ast::Constant
  build_constant(rxgrammar::RxParser::ConstantItemContext *context) {
    ast::Constant constant;
    stamp(constant, context);
    constant.name = context->identifier()->getText();
    constant.type = build_type(context->typeRef());
    constant.value = build_constant(context->constValue());
    return constant;
  }

  ast::Impl build_impl(rxgrammar::RxParser::InherentImplContext *context) {
    ast::Impl implementation;
    stamp(implementation, context);
    implementation.lifetime_parameters =
        lifetime_parameters(context->genericParams());
    implementation.target = build_type(context->typeRef());
    for (auto *item : context->associatedItem()) {
      if (item->functionDefinition())
        implementation.functions.push_back(
            build_function(item->functionDefinition()));
      else
        implementation.constants.push_back(
            build_constant(item->constantItem()));
    }
    return implementation;
  }

  const std::vector<std::string> &rule_names_;
  ast::NodeId next_id_{1};
};

} // namespace

bool lex_source(std::string_view source, DiagnosticEngine &diagnostics) {
  antlr4::ANTLRInputStream input{std::string(source)};
  rxgrammar::RxLexer lexer(&input);
  SyntaxListener listener(diagnostics);
  lexer.removeErrorListeners();
  lexer.addErrorListener(&listener);
  antlr4::CommonTokenStream token_stream(&lexer);
  token_stream.fill();
  for (auto *token : token_stream.getTokens()) {
    if (token->getType() == antlr4::Token::EOF)
      continue;
    const auto name = lexer.getVocabulary().getSymbolicName(token->getType());
    if (name == "INVALID_LIFETIME" || name == "INVALID_CHARACTER_LITERAL" ||
        name == "INVALID_NUMBER" || name == "UNTERMINATED_BLOCK_COMMENT" ||
        name == "ERROR_CHAR") {
      diagnostics.error({token->getStartIndex(), token->getStopIndex() + 1,
                         token->getLine(), token->getCharPositionInLine()},
                        "invalid token '" + token->getText() + "'");
    }
  }
  return !diagnostics.has_errors();
}

bool parse_syntax(std::string_view source, std::string_view entry,
                  DiagnosticEngine &diagnostics) {
  antlr4::ANTLRInputStream input{std::string(source)};
  rxgrammar::RxLexer lexer(&input);
  SyntaxListener listener(diagnostics);
  lexer.removeErrorListeners();
  lexer.addErrorListener(&listener);
  antlr4::CommonTokenStream token_stream(&lexer);
  rxgrammar::RxParser parser(&token_stream);
  parser.removeErrorListeners();
  parser.addErrorListener(&listener);
  if (entry == "crate")
    parser.crate();
  else if (entry == "item")
    parser.item();
  else if (entry == "expression")
    parser.expression();
  else if (entry == "letStatement")
    parser.letStatement();
  else if (entry == "typeRef")
    parser.typeRef();
  else {
    diagnostics.error({}, "unknown parser entry '" + std::string(entry) + "'");
    return false;
  }
  auto *trailing = parser.getCurrentToken();
  if (trailing && trailing->getType() != antlr4::Token::EOF) {
    diagnostics.error({trailing->getStartIndex(), trailing->getStopIndex() + 1,
                       trailing->getLine(), trailing->getCharPositionInLine()},
                      "unexpected trailing token '" + trailing->getText() +
                          "'");
  }
  return !diagnostics.has_errors();
}

FrontendResult parse_source(std::string_view source,
                            DiagnosticEngine &diagnostics) {
  antlr4::ANTLRInputStream input{std::string(source)};
  rxgrammar::RxLexer lexer(&input);
  SyntaxListener listener(diagnostics);
  lexer.removeErrorListeners();
  lexer.addErrorListener(&listener);

  antlr4::CommonTokenStream token_stream(&lexer);
  rxgrammar::RxParser parser(&token_stream);
  parser.removeErrorListeners();
  parser.addErrorListener(&listener);
  auto *tree = parser.crate();
  if (diagnostics.has_errors())
    return {{}, false};

  auto crate = AstBuilder(parser.getRuleNames()).build(tree);
  return {std::move(crate), true};
}

} // namespace rx
