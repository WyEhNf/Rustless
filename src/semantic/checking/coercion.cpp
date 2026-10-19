#include "semantic/analyzer.hpp"

namespace rx::semantic_detail {

SemanticTypePtr error_type() {
  static const auto value = [] {
    TypeContext context;
    return context.primitive(SemanticType::Kind::Error);
  }();
  return value;
}

bool compatible_types(const SemanticTypePtr &lhs, const SemanticTypePtr &rhs) {

  return lhs && rhs &&
         (lhs->kind() == SemanticType::Kind::Error ||
          rhs->kind() == SemanticType::Kind::Error || same_type(lhs, rhs));
}

bool integer(const SemanticTypePtr &value) {
  return value && (value->kind() == SemanticType::Kind::I32 ||
                   value->kind() == SemanticType::Kind::U32 ||
                   value->kind() == SemanticType::Kind::Isize ||
                   value->kind() == SemanticType::Kind::Usize);
}

bool signed_integer(const SemanticTypePtr &value) {
  return value && (value->kind() == SemanticType::Kind::I32 ||
                   value->kind() == SemanticType::Kind::Isize);
}

// check if from type A can be coerced to to type B.
bool Analyzer::coercible(const SemanticTypePtr &from,
                         const SemanticTypePtr &to) {
  if (!from || !to || from->kind() == SemanticType::Kind::Error ||
      to->kind() == SemanticType::Kind::Error ||
      from->kind() == SemanticType::Kind::Never)
    return true;
  if (compatible_types(from, to))
    return true;
  if (from->kind() == SemanticType::Kind::Ref &&
      to->kind() == SemanticType::Kind::Ref) {
    if (from->is_mutable() && !to->is_mutable() &&
        compatible_types(from->element(), to->element()))
      return true;
    auto current = from;
    while (current->kind() == SemanticType::Kind::Ref ||
           current->kind() == SemanticType::Kind::Box) {
      current = current->element();
      if (to->kind() == SemanticType::Kind::Ref &&
          (!to->is_mutable() || from->is_mutable()) &&
          compatible_types(current, to->element()))
        return true;
    }
  }
  return false;
}

//find a more basic type.
SemanticTypePtr Analyzer::common_type(const SemanticTypePtr &first,
                                      const SemanticTypePtr &second) {
  if (coercible(second, first))
    return first;
  if (coercible(first, second))
    return second;
  return primitive_type(SemanticType::Kind::Error);
}

bool Analyzer::writable(const ExprInfo &value) const {
  if (value.access_locked)
    return false;
  auto current = value.type;
  while (current && current->kind() == SemanticType::Kind::Box)
    current = current->element();
  if (current && current->kind() == SemanticType::Kind::Ref)
    return current->is_mutable();
  return !value.is_place || value.is_mutable;
}

void Analyzer::mismatch(Span span, const SemanticTypePtr &actual,
                        const SemanticTypePtr &expected) {
  report(span, "type mismatch: found " + type_name(actual) + ", expected " +
                   type_name(expected));
}

/*
transport one type to another may cost multiple steps.
the vec<adj> describes the process.
*/
std::vector<Adjustment>
Analyzer::coercion_adjustments(const SemanticTypePtr &from,
                               const SemanticTypePtr &to) const {
  std::vector<Adjustment> result;
  if (!from || !to || compatible_types(from, to) ||
      from->kind() == SemanticType::Kind::Error ||
      to->kind() == SemanticType::Kind::Error)
    return result;
  if (from->kind() == SemanticType::Kind::Never) {
    result.push_back({Adjustment::Kind::NeverToAny, to});
    return result;
  }
  if (from->kind() == SemanticType::Kind::Ref &&
      to->kind() == SemanticType::Kind::Ref && from->is_mutable() &&
      !to->is_mutable() && compatible_types(from->element(), to->element())) {
    result.push_back({Adjustment::Kind::MutToShared, to});
    return result;
  }
  if (from->kind() == SemanticType::Kind::Ref &&
      to->kind() == SemanticType::Kind::Ref) {
    auto current = from;
    while (current->kind() == SemanticType::Kind::Ref ||
           current->kind() == SemanticType::Kind::Box) {
      current = current->element();
      result.push_back({Adjustment::Kind::Dereference, current});
      if (compatible_types(current, to->element())) {
        auto borrow = Adjustment::Kind::BorrowShared;
        if (to->is_mutable())
          borrow = Adjustment::Kind::BorrowMutable;
        result.push_back({borrow, to});
        return result;
      }
    }
  }
  result.clear();
  return result;
}

} // namespace rx::semantic_detail
