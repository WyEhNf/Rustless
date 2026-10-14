#include "detail/analyzer.hpp"

namespace rx::semantic_detail {

SemanticTypePtr Analyzer::primitive_type(SemanticType::Kind kind){
  return types_.primitive(kind);
}

SemanticTypePtr Analyzer::compound(SemanticType::Kind kind, SemanticTypePtr element,
                         bool mutable_ref){
  switch (kind) {
  case SemanticType::Kind::Box:
    return types_.box(std::move(element));
  case SemanticType::Kind::Vec:
    return types_.vector(std::move(element));
  case SemanticType::Kind::Array:
    return types_.array(std::move(element), 0);
  case SemanticType::Kind::Ref: {
    auto mutability = ast::Mutability::Immutable;
    if (mutable_ref)
      mutability = ast::Mutability::Mutable;
    return types_.reference(std::move(element), mutability);
  }
  default:
    throw std::logic_error("invalid compound type factory");
  }
}

SemanticTypePtr Analyzer::structure_type(const std::string &name){
  if (auto found = structures_.find(name); found != structures_.end())
    return types_.structure(symbol_for(found->second.ast->id), name);
  return primitive_type(SemanticType::Kind::Error);
}

SemanticTypePtr Analyzer::resolve_type(const ast::TypePtr &syntax,
                             std::string_view self_name){
  auto result = resolve_type_impl(syntax, self_name);
  resolved_types_[syntax->id] = result;
  return result;
}

std::vector<const ast::TypePtr *>
Analyzer::type_arguments(const std::optional<ast::GenericArguments> &arguments){
  std::vector<const ast::TypePtr *> result;
  if (arguments)
    for (const auto &argument : arguments->values)
      if (auto *type = std::get_if<ast::TypePtr>(&argument))
        result.push_back(type);
  return result;
}

std::vector<const ast::TypePtr *>
Analyzer::type_arguments(const ast::Path &path){
  std::vector<const ast::TypePtr *> result;
  for (const auto &segment : path.segments) {
    auto arguments = type_arguments(segment.arguments);
    result.insert(result.end(), arguments.begin(), arguments.end());
  }
  return result;
}

SemanticTypePtr Analyzer::resolve_type(const std::optional<ast::TypePtr> &syntax,
                             std::string_view self_name){
  if (syntax)
    return resolve_type(*syntax, self_name);
  return primitive_type(SemanticType::Kind::Unit);
}

SemanticTypePtr Analyzer::resolve_type_impl(const ast::TypePtr &syntax,
                                  std::string_view self_name){
  return ast::visit(
      *syntax,
      ast::Overloaded{
          [&](const ast::TypeSyntax &, const ast::UnitType &) {
            return primitive_type(SemanticType::Kind::Unit);
          },
          [&](const ast::TypeSyntax &, const ast::ParenType &payload) {
            return resolve_type(payload.inner, self_name);
          },
          [&](const ast::TypeSyntax &, const ast::ReferenceType &payload) {
            return compound(SemanticType::Kind::Ref,
                            resolve_type(payload.referent, self_name),
                            payload.mutability == ast::Mutability::Mutable);
          },
          [&](const ast::TypeSyntax &, const ast::ArrayType &payload) {
            auto element = resolve_type(payload.element, self_name);
            auto length = evaluate_length(payload.length, self_name);
            return types_.array(std::move(element), length.value_or(0));
          },
          [&](const ast::TypeSyntax &node, const ast::PathType &payload) {
            return resolve_path_type(payload.path, node.span, self_name);
          }});
}

SemanticTypePtr Analyzer::resolve_path_type(const ast::Path &path, Span span,
                                  std::string_view self_name){
  const std::string name{ast::name_text(path.segments.back().name)};
  auto arguments = type_arguments(path);
  if (name == "Self") {
    if (self_name.empty()) {
      report(span, "Self is only available inside a struct or impl");
      return primitive_type(SemanticType::Kind::Error);
    }
    return structure_type(std::string(self_name));
  }
  static const std::unordered_map<std::string, SemanticType::Kind>
      primitives = {{"bool", SemanticType::Kind::Bool},
                    {"i32", SemanticType::Kind::I32},
                    {"u32", SemanticType::Kind::U32},
                    {"isize", SemanticType::Kind::Isize},
                    {"usize", SemanticType::Kind::Usize}};
  if (auto found = primitives.find(name); found != primitives.end()) {
    if (!arguments.empty())
      report(span, "primitive type '" + name + "' takes no type arguments");
    return primitive_type(found->second);
  }
  if (name == "Box" || name == "Vec") {
    if (arguments.size() != 1) {
      report(span, name + " requires exactly one concrete type argument");
      return primitive_type(SemanticType::Kind::Error);
    }
    auto element = resolve_type(*arguments.front(), self_name);
    if (name == "Box")
      return types_.box(std::move(element));
    return types_.vector(std::move(element));
  }
  if (structures_.find(name) != structures_.end()) {
    if (!arguments.empty())
      report(span, "struct '" + name + "' has no type parameters");
    return structure_type(name);
  }
  report(span, "unknown type '" + name + "'");
  return primitive_type(SemanticType::Kind::Error);
}

} // namespace rx::semantic_detail
