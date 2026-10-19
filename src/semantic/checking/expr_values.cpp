#include "semantic/analyzer.hpp"

namespace rx::semantic_detail {

ExprInfo Analyzer::check_array(const ast::Expr &node,
                               const ast::ArrayExpr &array,
                               const SemanticTypePtr &expected) {
  SemanticTypePtr expected_element;
  if (expected && expected->kind() == SemanticType::Kind::Array)
    expected_element = expected->element();
  if (array.elements.empty()) {
    if (!expected_element) {
      report(node.span, "an empty array requires an expected array type");
      return {};
    }
    return {compound(SemanticType::Kind::Array, expected_element)};
  }
  auto first = check_expr(array.elements.front(), expected_element);

  for (std::size_t index = 1; index < array.elements.size(); ++index) {
    auto element = check_expr(array.elements[index], expected_element);
    auto merged = common_type(first.type, element.type);
    if (merged->kind() == SemanticType::Kind::Error)
      mismatch(array.elements[index]->span, element.type, first.type);
    else {
      if (!compatible_types(element.type, merged))
        append_adjustments(array.elements[index]->id,
                           coercion_adjustments(element.type, merged));
      first.type = merged;
    }
  }
  auto result = types_.array(first.type, array.elements.size());
  if (expected_element &&
      (result->length() != expected->length() || !coercible(result, expected)))
    mismatch(node.span, result, expected);
  return {result};
}

ExprInfo Analyzer::check_repeat_array(const ast::Expr &node,
                                      const ast::RepeatArrayExpr &array,
                                      const SemanticTypePtr &expected) {
  SemanticTypePtr expected_element;
  if (expected && expected->kind() == SemanticType::Kind::Array)
    expected_element = expected->element();
  auto first = check_expr(array.element, expected_element);
  check_expr(array.count, primitive_type(SemanticType::Kind::Usize));
  auto length = evaluate_length(array.count, current_owner());
  auto result = types_.array(first.type, length.value_or(0));
  if (result->length() > 1 && !supports(first.type, "Copy", "", {}))
    report(node.span, "array repetition requires Copy");
  if (expected_element &&
      (result->length() != expected->length() || !coercible(result, expected)))
    mismatch(node.span, result, expected);
  return {result};
}

ExprInfo Analyzer::check_struct(const ast::Expr &node,
                                const ast::StructExpr &structure) {
  auto name = ast::path_name(structure.path);
  if (name == "Self" && current_function_ && !current_function_->owner.empty())
    name = current_function_->owner;
  auto found = structures_.find(name);
  if (found == structures_.end()) {
    report(node.span, "struct literal names an unknown struct '" + name + "'");
    return {};
  }
  std::unordered_set<std::string> initialized;
  for (const auto &field : structure.fields) {
    if (!initialized.insert(field.name.text).second)
      report(field.span,
             "duplicate field initializer '" + field.name.text + "'");
    auto expected = found->second.fields.find(field.name.text);
    if (expected == found->second.fields.end()) {
      report(field.span, "unknown field '" + field.name.text + "'");
      check_expr(field.value);
    } else {
      auto value = check_expr(field.value, expected->second);
      if (!coercible(value.type, expected->second))
        mismatch(field.value->span, value.type, expected->second);
    }
  }
  for (const auto &[field, _] : found->second.fields)
    if (initialized.find(field) == initialized.end())
      report(node.span, "missing field initializer '" + field + "'");
  auto result = structure_type(name);
  return {result, false, false, false, symbol_for(found->second.ast->id), name};
}

ExprInfo Analyzer::check_field(const ast::Expr &node,
                               const ast::FieldExpr &projection) {
  auto receiver = check_expr(projection.base);
  auto base = receiver.type;
  bool mutable_access = true;
  if (receiver.is_place)
    mutable_access = receiver.is_mutable;
  bool locked = receiver.access_locked;
  std::vector<Adjustment> adjustments;
  while (base->kind() == SemanticType::Kind::Ref ||
         base->kind() == SemanticType::Kind::Box) {
    if (base->kind() == SemanticType::Kind::Ref) {
      if (!base->is_mutable()) {
        locked = true;
        mutable_access = false;
      } else if (!locked) {

        mutable_access = true;
      }
    }
    base = base->element();
    adjustments.push_back({Adjustment::Kind::Dereference, base});
  }
  if (base->kind() != SemanticType::Kind::Struct ||
      structures_.find(base->name()) == structures_.end()) {
    report(node.span, "field access requires a struct");
    return {};
  }
  auto field = structures_.at(base->name()).fields.find(projection.field.text);
  if (field == structures_.at(base->name()).fields.end()) {
    report(node.span, "unknown field '" + projection.field.text + "'");
    return {};
  }
  ExprInfo result{
      field->second,
      true,
      mutable_access,
      locked,
      structures_.at(base->name()).field_symbols.at(projection.field.text),
      base->name() + "::" + projection.field.text};
  result.receiver =
      OperandSemantics{projection.base->id, base, std::move(adjustments)};
  return result;
}

ExprInfo Analyzer::check_index(const ast::Expr &node,
                               const ast::IndexExpr &projection) {
  auto base = check_expr(projection.base);
  auto index =
      check_expr(projection.index, primitive_type(SemanticType::Kind::Usize));
  if (!compatible_types(index.type, primitive_type(SemanticType::Kind::Usize)))
    mismatch(projection.index->span, index.type,
             primitive_type(SemanticType::Kind::Usize));
  auto container = base.type;
  bool mutable_access = true;
  if (base.is_place)
    mutable_access = base.is_mutable;
  bool locked = base.access_locked;
  std::vector<Adjustment> adjustments;
  while (container->kind() == SemanticType::Kind::Ref ||
         container->kind() == SemanticType::Kind::Box) {
    if (container->kind() == SemanticType::Kind::Ref) {
      if (!container->is_mutable()) {
        locked = true;
        mutable_access = false;
      } else if (!locked) {
        mutable_access = true;
      }
    }
    container = container->element();
    adjustments.push_back({Adjustment::Kind::Dereference, container});
  }
  if (container->kind() != SemanticType::Kind::Array &&
      container->kind() != SemanticType::Kind::Vec) {
    report(node.span, "type " + type_name(container) + " cannot be indexed");
    return {};
  }
  if (container->kind() == SemanticType::Kind::Vec && !mutable_access)
    locked = true;
  ExprInfo result{
      container->element(), true, mutable_access, locked, {}, "index"};
  result.receiver =
      OperandSemantics{projection.base->id, container, std::move(adjustments)};
  return result;
}

} // namespace rx::semantic_detail
