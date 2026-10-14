#include "rx/ast/frontend.hpp"

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

bool has_prefix(std::string_view text, std::string_view prefix) {
  return text.compare(0, prefix.size(), prefix) == 0;
}

bool has_suffix(std::string_view text, std::string_view suffix) {
  if (text.size() < suffix.size())
    return false;
  return text.compare(text.size() - suffix.size(), suffix.size(), suffix) == 0;
}

class SyntaxListener final : public antlr4::BaseErrorListener {
public:
  explicit SyntaxListener(DiagnosticEngine &diagnostics) : diagnostics_(diagnostics) {}

  void syntaxError(antlr4::Recognizer *, antlr4::Token *offending, std::size_t line,
                   std::size_t column, const std::string &message, std::exception_ptr) override {
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

Span raw_token_span(const antlr4::Token *token) {
  return {token->getStartIndex(), token->getStopIndex() + 1, token->getLine(),
          token->getCharPositionInLine()};
}

Span raw_source_span(ParseContext *context) {
  const auto *start = context->getStart();
  const auto *stop = context->getStop();
  if (!start)
    return {};
  const auto begin = start->getStartIndex();
  auto end = begin;
  if (stop && stop->getType() != antlr4::Token::EOF)
    end = stop->getStopIndex() + 1;
  return {begin, end, start->getLine(), start->getCharPositionInLine()};
}

Span merge(Span first, Span second) {
  first.end = second.end;
  return first;
}

class AstBuilder {
public:
  AstBuilder(const std::vector<std::string> &rule_names, std::string_view source)
      : rule_names_(rule_names), source_(std::string(source)) {}

  std::unique_ptr<ast::Crate> build(rxgrammar::RxParser::CrateContext *context) {
    auto crate = std::make_unique<ast::Crate>(source_.text);
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
      std::visit([&](const auto &item) { crate->span = item.span; }, crate->items.front());
      std::visit([&](const auto &item) { crate->span.end = item.span.end; }, crate->items.back());
    }
    return crate;
  }

private:
  Span source_span(ParseContext *context) const {
    return source_.from_character_span(raw_source_span(context));
  }
  Span token_span(const antlr4::Token *token) const {
    return source_.from_character_span(raw_token_span(token));
  }
  template <typename Node> void stamp(Node &node, ParseContext *context) {
    node.id = ast::NodeId{next_id_++};
    node.span = source_span(context);
  }

  std::string_view rule(ParseContext *context) const {
    return rule_names_.at(context->getRuleIndex());
  }

  static ParseContext *as_context(ParseTree *tree) { return dynamic_cast<ParseContext *>(tree); }

  static TerminalNode *as_terminal(ParseTree *tree) { return dynamic_cast<TerminalNode *>(tree); }

  bool is_expression_context(ParseContext *context) const {
    const auto name = rule(context);
    return name == "expression" || has_suffix(name, "Expression") || name == "nonBlockPrimary" ||
           name == "conditionPrimary" || name == "conditionPrimaryWithoutBareBlock";
  }

  static bool is_operator_rule(std::string_view name) {
    return name == "assignmentOperator" || name == "equalsSign" || name == "comparisonExceptLt" ||
           name == "shiftRight" || name == "additiveOperator" || name == "multiplicativeOperator" ||
           name == "genericClose";
  }

  static bool is_rule_family(std::string_view name, std::string_view prefix,
                             std::string_view nested_name) {
    return has_prefix(name, prefix) || name.find(nested_name) != std::string_view::npos;
  }

  static bool is_binary_rule(std::string_view name) {
    if (!has_suffix(name, "Expression"))
      return false;
    return is_rule_family(name, "logicalOr", "LogicalOr") ||
           is_rule_family(name, "logicalAnd", "LogicalAnd") ||
           is_rule_family(name, "comparison", "Comparison") ||
           is_rule_family(name, "bitOr", "BitOr") || is_rule_family(name, "bitXor", "BitXor") ||
           is_rule_family(name, "bitAnd", "BitAnd") || is_rule_family(name, "shift", "Shift") ||
           is_rule_family(name, "additive", "Additive") ||
           is_rule_family(name, "multiplicative", "Multiplicative");
  }

  template <class Payload> ast::ExprPtr make_expr(ParseContext *context, Payload payload) {
    auto expression = ast::box<ast::Expr>(ast::Expr::Data{std::move(payload)});
    stamp(*expression, context);
    return expression;
  }

  template <class Payload> ast::TypePtr make_type(ParseContext *context, Payload payload) {
    auto type = ast::box<ast::TypeSyntax>(ast::TypeSyntax::Data{std::move(payload)});
    stamp(*type, context);
    return type;
  }

  ast::Identifier identifier(ParseContext *context) {
    return {context->getText(), source_span(context)};
  }
  ast::LifetimeName lifetime(rxgrammar::RxParser::LifetimeContext *context) {
    return {context->getText(), source_span(context)};
  }
  ast::Binding binding(rxgrammar::RxParser::IdentifierBindingContext *context) {
    return {identifier(context->identifier()), mutability(context->MUT() != nullptr)};
  }
  ast::Mutability mutability(bool mutable_value) {
    if (mutable_value)
      return ast::Mutability::Mutable;
    return ast::Mutability::Immutable;
  }
  std::optional<ast::GenericParameters>
  build_generics(rxgrammar::RxParser::GenericParamsContext *context) {
    if (!context)
      return {};
    ast::GenericParameters result{source_span(context), {}};
    for (auto *parameter : context->lifetimeParam()) {
      ast::LifetimeParameter value{{}, lifetime(parameter->lifetime()), {}};
      stamp(value, parameter);
      if (auto *bounds = parameter->lifetimeBounds())
        for (auto *name : bounds->lifetime())
          value.bounds.push_back(lifetime(name));
      result.lifetimes.push_back(std::move(value));
    }
    return result;
  }
  std::optional<ast::WhereClause> build_where(rxgrammar::RxParser::WhereClauseContext *context) {
    if (!context)
      return {};
    ast::WhereClause result{source_span(context), {}};
    for (auto *predicate : context->whereClauseItem()) {
      std::vector<ast::LifetimeName> bounds;
      if (auto *list = predicate->lifetimeBounds())
        for (auto *name : list->lifetime())
          bounds.push_back(lifetime(name));
      if (auto *list = predicate->typeParamBounds())
        for (auto *name : list->lifetime())
          bounds.push_back(lifetime(name));
      if (predicate->lifetime()) {
        ast::LifetimePredicate value{{}, lifetime(predicate->lifetime()), std::move(bounds)};
        stamp(value, predicate);
        result.predicates.emplace_back(std::move(value));
      } else {
        ast::TypePredicate value{{}, build_type(predicate->typeRef()), std::move(bounds)};
        stamp(value, predicate);
        result.predicates.emplace_back(std::move(value));
      }
    }
    return result;
  }

  std::optional<ast::GenericArguments>
  build_arguments(rxgrammar::RxParser::GenericArgsContext *context) {
    if (!context)
      return {};
    ast::GenericArguments result{source_span(context), {}};
    for (auto *argument : context->genericArg()) {
      if (argument->typeRef())
        result.values.emplace_back(build_type(argument->typeRef()));
      else
        result.values.emplace_back(lifetime(argument->lifetime()));
    }
    return result;
  }
  ast::PathName path_ident(rxgrammar::RxParser::PathIdentSegmentContext *context) {
    if (context->SELF_VALUE())
      return ast::SelfValue{source_span(context)};
    if (context->SELF_TYPE())
      return ast::SelfType{source_span(context)};
    return identifier(context->identifier());
  }
  template <class Segment> ast::PathSegment path_segment(Segment *context) {
    return {path_ident(context->pathIdentSegment()), build_arguments(context->genericArgs()),
            source_span(context)};
  }
  template <class Context> ast::TypePtr reference_type(Context *context, ParseContext *referent) {
    std::optional<ast::LifetimeName> name;
    if (context->lifetime())
      name = lifetime(context->lifetime());
    auto inner = make_type(context, ast::ReferenceType{std::move(name),
                                                       mutability(context->MUT() != nullptr),
                                                       build_type(referent)});
    if (!context->ANDAND())
      return inner;
    ++inner->span.begin;
    ++inner->span.column;
    return make_type(context, ast::ReferenceType{{}, ast::Mutability::Immutable, std::move(inner)});
  }
  ast::TypePtr build_type(ParseContext *context) {
    if (auto *type = dynamic_cast<rxgrammar::RxParser::TypeRefContext *>(context)) {
      if (type->typeRef())
        return make_type(type, ast::ParenType{build_type(type->typeRef())});
      if (type->typePath()) {
        ast::Path path{source_span(type->typePath()), {}};
        for (auto *segment : type->typePath()->typePathSegment())
          path.segments.push_back(path_segment(segment));
        return make_type(type, ast::PathType{std::move(path)});
      }
      if (type->referenceType())
        return build_type(type->referenceType());
      if (type->arrayType())
        return build_type(type->arrayType());
      return make_type(type, ast::UnitType{});
    }
    if (auto *reference = dynamic_cast<rxgrammar::RxParser::ReferenceTypeContext *>(context))
      return reference_type(reference, reference->typeRef());
    if (auto *array = dynamic_cast<rxgrammar::RxParser::ArrayTypeContext *>(context))
      return make_type(
          array, ast::ArrayType{build_type(array->typeRef()), build_constant(array->constValue())});
    auto *closed = dynamic_cast<rxgrammar::RxParser::ClosedCastTypeContext *>(context);
    if (!closed)
      throw std::logic_error("unexpected type parse context");
    if (closed->typeRef())
      return make_type(closed, ast::ParenType{build_type(closed->typeRef())});
    if (closed->arrayType())
      return build_type(closed->arrayType());
    if (closed->closedCastType())
      return reference_type(closed, closed->closedCastType());
    if (closed->pathIdentSegment()) {
      ast::Path path{source_span(closed), {}};
      for (auto *segment : closed->typePathSegment())
        path.segments.push_back(path_segment(segment));
      path.segments.push_back({path_ident(closed->pathIdentSegment()),
                               build_arguments(closed->genericArgs()),
                               source_span(closed->pathIdentSegment())});
      return make_type(closed, ast::PathType{std::move(path)});
    }
    return make_type(closed, ast::UnitType{});
  }
  ast::Path expression_path(rxgrammar::RxParser::PathInExpressionContext *context) {
    ast::Path path{source_span(context), {}};
    for (auto *segment : context->pathExprSegment())
      path.segments.push_back(path_segment(segment));
    return path;
  }
  ast::ExprPtr build_path(rxgrammar::RxParser::PathInExpressionContext *context) {
    return make_expr(context, ast::PathExpr{expression_path(context)});
  }

  ast::ExprPtr integer_expr(ParseContext *context) {
    return make_expr(context, ast::IntegerExpr{ast::IntegerLiteral::parse(context->getText())});
  }
  ast::ExprPtr build_magnitude(rxgrammar::RxParser::MagnitudeContext *context) {
    if (context->INTEGER_LITERAL())
      return integer_expr(context);
    if (context->pathInExpression())
      return build_path(context->pathInExpression());
    return make_expr(context, ast::ParenExpr{build_magnitude(context->magnitude())});
  }
  ast::ExprPtr build_constant(rxgrammar::RxParser::ConstValueContext *context) {
    if (context->INTEGER_LITERAL())
      return integer_expr(context);
    if (context->TRUE() || context->FALSE())
      return make_expr(context, ast::BooleanExpr{context->TRUE() != nullptr});
    if (context->pathInExpression())
      return build_path(context->pathInExpression());
    if (context->MINUS())
      return make_expr(context, ast::UnaryExpr{ast::UnaryOp::Negate,
                                               token_span(context->MINUS()->getSymbol()),
                                               build_magnitude(context->magnitude())});
    return make_expr(context, ast::ParenExpr{build_constant(context->constValue())});
  }
  ast::ExprPtr build_literal(rxgrammar::RxParser::LiteralExpressionContext *context) {
    if (context->INTEGER_LITERAL())
      return integer_expr(context);
    return make_expr(context, ast::BooleanExpr{context->TRUE() != nullptr});
  }
  ast::ExprPtr build_array(rxgrammar::RxParser::ArrayExpressionContext *context) {
    if (context->SEMI())
      return make_expr(context, ast::RepeatArrayExpr{build_expression(context->expression(0)),
                                                     build_constant(context->constValue())});
    ast::ArrayExpr array;
    for (auto *element : context->expression())
      array.elements.push_back(build_expression(element));
    return make_expr(context, std::move(array));
  }
  ast::IfExpr build_if_data(rxgrammar::RxParser::IfExpressionContext *context) {
    ast::IfExpr result{build_expression(context->conditionExpression()),
                       build_block(context->blockExpression(0)),
                       {}};
    if (context->ifExpression()) {
      auto branch = ast::box<ast::IfBranch>(build_if_data(context->ifExpression()));
      stamp(*branch, context->ifExpression());
      result.else_branch.emplace(std::move(branch));
    } else if (context->blockExpression().size() > 1) {
      result.else_branch.emplace(build_block(context->blockExpression(1)));
    }
    return result;
  }
  ast::ExprPtr build_if(rxgrammar::RxParser::IfExpressionContext *context) {
    return make_expr(context, build_if_data(context));
  }
  ast::ExprPtr
  build_expression_with_block(rxgrammar::RxParser::ExpressionWithBlockContext *context) {
    if (context->ifExpression())
      return build_if(context->ifExpression());
    if (context->LOOP())
      return make_expr(context, ast::LoopExpr{build_block(context->blockExpression())});
    if (context->WHILE())
      return make_expr(context, ast::WhileExpr{build_expression(context->conditionExpression()),
                                               build_block(context->blockExpression())});
    return make_expr(context, ast::BlockExpr{build_block(context->blockExpression())});
  }

  ast::StmtPtr build_let(rxgrammar::RxParser::LetStatementContext *context) {
    std::optional<ast::TypePtr> annotation;
    if (context->typeRef())
      annotation.emplace(build_type(context->typeRef()));
    auto statement = ast::box<ast::Stmt>(ast::LetStmt{binding(context->identifierBinding()),
                                                      std::move(annotation),
                                                      build_expression(context->expression())});
    stamp(*statement, context);
    return statement;
  }
  ast::StmtPtr build_statement(rxgrammar::RxParser::StatementContext *context) {
    if (context->letStatement())
      return build_let(context->letStatement());
    if (!context->expressionWithBlock() && !context->statementExpression()) {
      auto statement = ast::box<ast::Stmt>(ast::EmptyStmt{});
      stamp(*statement, context);
      return statement;
    }
    auto expression = build_statement_expression(context);
    auto termination = ast::Termination::Omitted;
    if (context->SEMI())
      termination = ast::Termination::Semicolon;
    auto statement = ast::box<ast::Stmt>(ast::ExprStmt{std::move(expression), termination});
    stamp(*statement, context);
    return statement;
  }

  ast::ExprPtr build_statement_expression(rxgrammar::RxParser::StatementContext *context) {
    if (context->expressionWithBlock())
      return build_expression_with_block(context->expressionWithBlock());
    return build_expression(context->statementExpression());
  }
  ast::Box<ast::Block> build_block(rxgrammar::RxParser::BlockExpressionContext *context) {
    auto block = ast::box<ast::Block>();
    stamp(*block, context);
    for (auto *child : context->statement())
      block->statements.push_back(build_statement(child));
    if (context->statementExpression()) {
      block->tail.emplace(build_expression(context->statementExpression()));
    } else if (!block->statements.empty()) {
      auto *last = std::get_if<ast::ExprStmt>(&block->statements.back()->data);
      if (last && last->termination == ast::Termination::Omitted) {
        block->tail.emplace(std::move(last->expression));
        block->statements.pop_back();
      }
    }
    return block;
  }
  template <class Context> ast::ExprPtr build_jump(Context *context, ParseContext *value) {
    std::optional<ast::ExprPtr> operand;
    if (value)
      operand.emplace(build_expression(value));
    if (context->BREAK())
      return make_expr(context, ast::BreakExpr{std::move(operand)});
    return make_expr(context, ast::ReturnExpr{std::move(operand)});
  }

  ast::ExprPtr build_primary(ParseContext *context) {
    if (auto *primary = dynamic_cast<rxgrammar::RxParser::PrimaryExpressionContext *>(context)) {
      if (primary->nonBlockPrimary())
        return build_primary(primary->nonBlockPrimary());
      return build_expression_with_block(primary->expressionWithBlock());
    }
    if (auto *primary = dynamic_cast<rxgrammar::RxParser::ConditionPrimaryContext *>(context)) {
      if (primary->conditionPrimaryWithoutBareBlock())
        return build_primary(primary->conditionPrimaryWithoutBareBlock());
      return make_expr(primary, ast::BlockExpr{build_block(primary->blockExpression())});
    }
    if (auto *primary = dynamic_cast<rxgrammar::RxParser::NonBlockPrimaryContext *>(context)) {
      if (primary->literalExpression())
        return build_literal(primary->literalExpression());
      if (primary->pathInExpression()) {
        if (!primary->LBRACE())
          return build_path(primary->pathInExpression());
        ast::StructExpr structure{expression_path(primary->pathInExpression()), {}};
        if (primary->structExprFields())
          for (auto *field : primary->structExprFields()->structExprField()) {
            ast::FieldInit value{
                {}, identifier(field->identifier()), build_expression(field->expression())};
            stamp(value, field);
            structure.fields.push_back(std::move(value));
          }
        return make_expr(primary, std::move(structure));
      }
      if (primary->arrayExpression())
        return build_array(primary->arrayExpression());
      if (primary->BREAK() || primary->RETURN())
        return build_jump(primary, primary->expression());
      if (primary->CONTINUE())
        return make_expr(primary, ast::ContinueExpr{});
      if (primary->expression())
        return make_expr(primary, ast::ParenExpr{build_expression(primary->expression())});
      return make_expr(primary, ast::UnitExpr{});
    }
    auto *primary =
        dynamic_cast<rxgrammar::RxParser::ConditionPrimaryWithoutBareBlockContext *>(context);
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
    if (primary->LOOP())
      return make_expr(primary, ast::LoopExpr{build_block(primary->blockExpression())});
    if (primary->WHILE())
      return make_expr(primary, ast::WhileExpr{build_expression(primary->conditionExpression()),
                                               build_block(primary->blockExpression())});
    if (primary->BREAK())
      return build_jump(primary, primary->conditionBreakExpression());
    if (primary->RETURN())
      return build_jump(primary, primary->conditionExpression());
    if (primary->CONTINUE())
      return make_expr(primary, ast::ContinueExpr{});
    if (primary->expression())
      return make_expr(primary, ast::ParenExpr{build_expression(primary->expression())});
    return make_expr(primary, ast::UnitExpr{});
  }

  std::vector<ast::ExprPtr> call_arguments(rxgrammar::RxParser::CallArgumentsContext *context) {
    std::vector<ast::ExprPtr> values;
    for (auto *value : context->expression())
      values.push_back(build_expression(value));
    return values;
  }
  ast::ExprPtr apply_call(ast::ExprPtr base, rxgrammar::RxParser::CallArgumentsContext *context) {
    auto span = merge(base->span, source_span(context));
    auto call = make_expr(context, ast::CallExpr{std::move(base), call_arguments(context)});
    call->span = span;
    return call;
  }
  ast::ExprPtr apply_dot(ast::ExprPtr base, rxgrammar::RxParser::DotSuffixContext *context) {
    auto span = merge(base->span, source_span(context));
    if (!context->callArguments()) {
      auto field =
          make_expr(context, ast::FieldExpr{std::move(base), identifier(context->identifier())});
      field->span = span;
      return field;
    }
    auto *segment = context->pathExprSegment();
    auto method = make_expr(context, ast::MethodCallExpr{std::move(base),
                                                         identifier(segment->pathIdentSegment()),
                                                         build_arguments(segment->genericArgs()),
                                                         call_arguments(context->callArguments())});
    method->span = span;
    return method;
  }
  ast::ExprPtr apply_postfix(ast::ExprPtr base,
                             rxgrammar::RxParser::PostfixSuffixContext *context) {
    if (context->callArguments())
      return apply_call(std::move(base), context->callArguments());
    if (context->dotSuffix())
      return apply_dot(std::move(base), context->dotSuffix());
    auto span = merge(base->span, source_span(context));
    auto index = make_expr(
        context, ast::IndexExpr{std::move(base), build_expression(context->expression())});
    index->span = span;
    return index;
  }

  ast::ExprPtr build_postfix(ParseContext *context) {
    std::optional<ast::ExprPtr> result;
    for (auto *child : context->children) {
      auto *child_context = as_context(child);
      if (!child_context)
        continue;
      const auto name = rule(child_context);
      if (!result) {
        result.emplace(build_expression(child_context));
      } else if (name == "postfixSuffix") {
        result =
            apply_postfix(std::move(*result),
                          static_cast<rxgrammar::RxParser::PostfixSuffixContext *>(child_context));
      } else if (name == "dotSuffix") {
        result = apply_dot(std::move(*result),
                           static_cast<rxgrammar::RxParser::DotSuffixContext *>(child_context));
      }
    }
    if (!result)
      throw std::logic_error("missing postfix base");
    return std::move(*result);
  }

  ast::ExprPtr build_unary(ParseContext *context) {
    ParseContext *operator_context = nullptr;
    std::optional<ast::ExprPtr> operand;
    for (auto *child : context->children) {
      auto *child_context = as_context(child);
      if (!child_context)
        continue;
      if (rule(child_context) == "unaryOperator")
        operator_context = child_context;
      else if (is_expression_context(child_context))
        operand.emplace(build_expression(child_context));
    }
    if (!operand)
      throw std::logic_error("missing unary operand");
    if (!operator_context)
      return std::move(*operand);
    const auto operator_text = operator_context->getText();
    if (has_prefix(operator_text, "&")) {
      auto inner =
          make_expr(context, ast::BorrowExpr{mutability(has_suffix(operator_text, "mut")),
                                             source_span(operator_context), std::move(*operand)});
      if (has_prefix(operator_text, "&&")) {
        ++inner->span.begin;
        ++inner->span.column;
        ++std::get<ast::BorrowExpr>(inner->data).operator_span.begin;
        ++std::get<ast::BorrowExpr>(inner->data).operator_span.column;
        auto outer_span = source_span(operator_context);
        outer_span.end = outer_span.begin + 1;
        return make_expr(context,
                         ast::BorrowExpr{ast::Mutability::Immutable, outer_span, std::move(inner)});
      }
      return inner;
    }
    return make_expr(context, ast::UnaryExpr{ast::unary_operator(operator_text),
                                             source_span(operator_context), std::move(*operand)});
  }
  ast::ExprPtr build_cast(ParseContext *context) {
    std::optional<ast::ExprPtr> result;
    for (auto *child : context->children) {
      auto *child_context = as_context(child);
      if (!child_context)
        continue;
      auto name = rule(child_context);
      if (is_expression_context(child_context))
        result.emplace(build_expression(child_context));
      else if (name == "typeRef" || name == "closedCastType") {
        if (!result)
          throw std::logic_error("missing cast operand");
        auto span = merge((*result)->span, source_span(child_context));
        auto cast =
            make_expr(context, ast::CastExpr{std::move(*result), build_type(child_context)});
        cast->span = span;
        result = std::move(cast);
      }
    }
    if (!result)
      throw std::logic_error("missing cast expression");
    return std::move(*result);
  }
  ast::ExprPtr fold_binary(ParseContext *context) {
    std::optional<ast::ExprPtr> result;
    std::string operator_text;
    Span operator_span;
    for (auto *child : context->children) {
      if (auto *terminal = as_terminal(child)) {
        operator_text = terminal->getText();
        operator_span = token_span(terminal->getSymbol());
        continue;
      }
      auto *child_context = as_context(child);
      if (!child_context)
        continue;
      if (is_operator_rule(rule(child_context))) {
        operator_text = child_context->getText();
        operator_span = source_span(child_context);
        continue;
      }
      if (!is_expression_context(child_context))
        continue;
      auto operand = build_expression(child_context);
      if (!result) {
        result.emplace(std::move(operand));
        continue;
      }
      auto span = merge((*result)->span, operand->span);
      auto binary =
          make_expr(context, ast::BinaryExpr{ast::binary_operator(operator_text), operator_span,
                                             std::move(*result), std::move(operand)});
      binary->span = span;
      result = std::move(binary);
    }
    if (!result)
      throw std::logic_error("missing binary expression");
    return std::move(*result);
  }
  ast::ExprPtr build_assignment(ParseContext *context) {
    std::optional<ast::ExprPtr> left;
    std::optional<ast::ExprPtr> right;
    ParseContext *operator_context = nullptr;
    for (auto *child : context->children) {
      auto *child_context = as_context(child);
      if (!child_context)
        continue;
      if (is_operator_rule(rule(child_context)))
        operator_context = child_context;
      else if (is_expression_context(child_context)) {
        if (!left)
          left.emplace(build_expression(child_context));
        else
          right.emplace(build_expression(child_context));
      }
    }
    if (!left)
      throw std::logic_error("missing assignment target");
    if (!right)
      return std::move(*left);
    const auto operator_text = operator_context->getText();
    if (operator_text == "=")
      return make_expr(context, ast::AssignExpr{std::move(*left), std::move(*right)});
    return make_expr(context, ast::CompoundAssignExpr{ast::compound_operator(operator_text),
                                                      source_span(operator_context),
                                                      std::move(*left), std::move(*right)});
  }

  ast::ExprPtr build_expression(ParseContext *context) {
    const auto name = rule(context);
    if (name == "blockExpression")
      return make_expr(context,
                       ast::BlockExpr{build_block(
                           static_cast<rxgrammar::RxParser::BlockExpressionContext *>(context))});
    if (name == "expressionWithBlock")
      return build_expression_with_block(
          static_cast<rxgrammar::RxParser::ExpressionWithBlockContext *>(context));
    if (name == "ifExpression")
      return build_if(static_cast<rxgrammar::RxParser::IfExpressionContext *>(context));
    if (name == "literalExpression")
      return build_literal(static_cast<rxgrammar::RxParser::LiteralExpressionContext *>(context));
    if (name == "arrayExpression")
      return build_array(static_cast<rxgrammar::RxParser::ArrayExpressionContext *>(context));
    if (name == "primaryExpression" || name == "nonBlockPrimary" || name == "conditionPrimary" ||
        name == "conditionPrimaryWithoutBareBlock")
      return build_primary(context);
    if (name == "postfixExpression" || name.find("PostfixExpression") != std::string_view::npos)
      return build_postfix(context);
    if (name == "unaryExpression" || name.find("UnaryExpression") != std::string_view::npos)
      return build_unary(context);
    if (name == "castExpression" || name.find("CastExpression") != std::string_view::npos)
      return build_cast(context);
    if (name == "assignmentExpression" ||
        name.find("AssignmentExpression") != std::string_view::npos)
      return build_assignment(context);
    if (is_binary_rule(name))
      return fold_binary(context);
    for (auto *child : context->children)
      if (auto *child_context = as_context(child);
          child_context && is_expression_context(child_context))
        return build_expression(child_context);
    throw std::logic_error("unsupported expression parse context: " + std::string(name));
  }

  ast::UseTree build_use_tree(rxgrammar::RxParser::UseTreeContext *context) {
    ast::UseTree tree;
    stamp(tree, context);
    tree.prefix.span = source_span(context);
    tree.prefix.root = ast::PathRoot::Relative;
    if (context->getStart()->getText() == "::")
      tree.prefix.root = ast::PathRoot::Absolute;
    if (auto *path = context->usePath()) {
      tree.prefix.span = source_span(path);
      for (auto *segment : path->usePathSegment()) {
        if (segment->identifier()) {
          tree.prefix.segments.emplace_back(identifier(segment->identifier()));
        } else {
          auto keyword = ast::UseKeyword::Crate;
          if (segment->SELF_VALUE())
            keyword = ast::UseKeyword::Self;
          else if (segment->SUPER())
            keyword = ast::UseKeyword::Super;
          tree.prefix.segments.emplace_back(ast::UseKeywordName{keyword, source_span(segment)});
        }
      }
    }
    if (context->STAR())
      tree.target = ast::GlobImport{};
    else if (context->LBRACE()) {
      ast::GroupImport group;
      for (auto *child : context->useTree())
        group.children.push_back(build_use_tree(child));
      tree.target = std::move(group);
    } else {
      ast::NameImport name;
      if (context->identifier())
        name.alias.emplace(identifier(context->identifier()));
      else if (context->UNDERSCORE())
        name.alias.emplace(ast::UnderscoreAlias{token_span(context->UNDERSCORE()->getSymbol())});
      tree.target = std::move(name);
    }
    return tree;
  }
  ast::Use build_use(rxgrammar::RxParser::UseDeclarationContext *context) {
    ast::Use use{{}, build_use_tree(context->useTree())};
    stamp(use, context);
    return use;
  }
  ast::Parameter build_parameter(rxgrammar::RxParser::FunctionParamContext *context) {
    ast::Parameter parameter{
        {}, binding(context->identifierBinding()), build_type(context->typeRef())};
    stamp(parameter, context);
    return parameter;
  }
  ast::Function build_function(rxgrammar::RxParser::FunctionDefinitionContext *context) {
    ast::FunctionSignature signature;
    if (auto *parameters = context->functionParameters()) {
      if (auto *self = parameters->selfParam()) {
        ast::SelfParam receiver;
        stamp(receiver, self);
        if (self->AMP()) {
          std::optional<ast::LifetimeName> name;
          if (self->lifetime())
            name = lifetime(self->lifetime());
          receiver.data = ast::ReferenceSelf{mutability(self->MUT() != nullptr), std::move(name)};
        } else
          receiver.data = ast::ValueSelf{mutability(self->MUT() != nullptr)};
        signature.receiver = std::move(receiver);
      }
      for (auto *parameter : parameters->functionParam())
        signature.parameters.push_back(build_parameter(parameter));
    }
    if (context->typeRef())
      signature.return_annotation.emplace(build_type(context->typeRef()));
    ast::Function function{{},
                           identifier(context->identifier()),
                           build_generics(context->genericParams()),
                           std::move(signature),
                           build_where(context->whereClause()),
                           build_block(context->blockExpression())};
    stamp(function, context);
    return function;
  }
  ast::Struct build_struct(rxgrammar::RxParser::StructDefinitionContext *context) {
    ast::Struct structure{{},
                          identifier(context->identifier()),
                          {},
                          build_generics(context->genericParams()),
                          build_where(context->whereClause()),
                          {}};
    stamp(structure, context);
    for (auto *attribute : context->outerAttribute()) {
      ast::DeriveAttribute value{source_span(attribute), {}};
      for (auto *name : attribute->deriveName())
        value.names.push_back({ast::derive_kind(name->getText()), source_span(name)});
      structure.attributes.push_back(std::move(value));
    }
    for (auto *field : context->structField()) {
      ast::StructField value{{}, identifier(field->identifier()), build_type(field->typeRef())};
      stamp(value, field);
      structure.fields.push_back(std::move(value));
    }
    return structure;
  }
  ast::Constant build_constant(rxgrammar::RxParser::ConstantItemContext *context) {
    ast::Constant value{{},
                        identifier(context->identifier()),
                        build_type(context->typeRef()),
                        build_constant(context->constValue())};
    stamp(value, context);
    return value;
  }
  ast::Impl build_impl(rxgrammar::RxParser::InherentImplContext *context) {
    ast::Impl value{{},
                    build_generics(context->genericParams()),
                    build_type(context->typeRef()),
                    build_where(context->whereClause()),
                    {}};
    stamp(value, context);
    for (auto *item : context->associatedItem()) {
      if (item->functionDefinition())
        value.members.emplace_back(build_function(item->functionDefinition()));
      else
        value.members.emplace_back(build_constant(item->constantItem()));
    }
    return value;
  }

  const std::vector<std::string> &rule_names_;
  ast::SourceFile source_;
  std::uint32_t next_id_{1};
};

}

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
        name == "INVALID_NUMBER" || name == "UNTERMINATED_BLOCK_COMMENT" || name == "ERROR_CHAR") {
      diagnostics.error({token->getStartIndex(), token->getStopIndex() + 1, token->getLine(),
                         token->getCharPositionInLine()},
                        "invalid token '" + token->getText() + "'");
    }
  }
  return !diagnostics.has_errors();
}

bool parse_syntax(std::string_view source, std::string_view entry, DiagnosticEngine &diagnostics) {
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
    diagnostics.error({trailing->getStartIndex(), trailing->getStopIndex() + 1, trailing->getLine(),
                       trailing->getCharPositionInLine()},
                      "unexpected trailing token '" + trailing->getText() + "'");
  }
  return !diagnostics.has_errors();
}

FrontendResult parse_source(std::string_view source, DiagnosticEngine &diagnostics) {
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

  auto crate = AstBuilder(parser.getRuleNames(), source).build(tree);
  return {std::move(crate), true};
}

}
