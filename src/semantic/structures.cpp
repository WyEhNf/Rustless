#include "rx/semantic/analyzer.hpp"

namespace rx::semantic_detail {

void Analyzer::validate_layouts_and_derives() {
  for (auto &[name, _] : structures_)
    validate_layout(name);
  for (auto &[name, structure] : structures_) {
    for (const auto &derive : structure.derives) {
      for (const auto &[_, field] : structure.fields) {
        if (!supports(field, derive, name, {})) {
          report(structure.ast->span, "field type " + type_name(field) +
                                          " does not support derived " +
                                          derive);
          break;
        }
      }
    }
  }
}

bool Analyzer::validate_layout(const std::string &name) {

  auto &structure = structures_.at(name);
  if (structure.layout == StructInfo::LayoutState::Valid)
    return true;
  if (structure.layout == StructInfo::LayoutState::Invalid)
    return false;
  if (structure.layout == StructInfo::LayoutState::Visiting) {
    report(structure.ast->span, "recursive type has infinite inline layout");
    structure.layout = StructInfo::LayoutState::Invalid;
    return false;
  }
  structure.layout = StructInfo::LayoutState::Visiting;
  bool valid = true;
  std::function<void(const SemanticTypePtr &)> visit =
      [&](const SemanticTypePtr &field) {
        if (field->kind() == SemanticType::Kind::Struct)
          valid &= validate_layout(field->name());
        else if (field->kind() == SemanticType::Kind::Array)
          visit(field->element());
      };
  for (const auto &[_, field] : structure.fields)
    visit(field);
  structure.layout = StructInfo::LayoutState::Invalid;
  if (valid)
    structure.layout = StructInfo::LayoutState::Valid;
  return valid;
}

bool Analyzer::supports(const SemanticTypePtr &value, const std::string &trait,
                        const std::string &root,
                        std::unordered_set<std::string> seen) {
  if (!value)
    return false;
  if (value->kind() == SemanticType::Kind::Bool || integer(value) ||
      value->kind() == SemanticType::Kind::Unit)
    return true;
  if (value->kind() == SemanticType::Kind::Ref) {
    if (trait == "Copy")
      return !value->is_mutable();
    if (trait == "Clone")
      return !value->is_mutable();
    if (trait == "PartialEq" || trait == "Eq")
      return supports(value->element(), trait, root, seen);
  }
  if (value->kind() == SemanticType::Kind::Box ||
      value->kind() == SemanticType::Kind::Vec ||
      value->kind() == SemanticType::Kind::Array) {
    if (trait == "Copy" && value->kind() != SemanticType::Kind::Array)
      return false;
    return supports(value->element(), trait, root, std::move(seen));
  }
  if (value->kind() == SemanticType::Kind::Struct) {
    if (seen.find(value->name()) != seen.end())
      return trait != "Copy";
    seen.insert(value->name());
    auto found = structures_.find(value->name());
    if (found == structures_.end() ||
        found->second.derives.find(trait) == found->second.derives.end())
      return false;
    for (const auto &[_, field] : found->second.fields)
      if (!supports(field, trait, root, seen))
        return false;
    return true;
  }
  return false;
}

void Analyzer::resolve_structures() {
  for (auto &[name, structure] : structures_) {
    std::unordered_set<std::string> fields;
    std::unordered_set<std::string> derives;
    for (const auto &attribute : structure.ast->attributes)
      for (const auto &name : attribute.names) {
        const std::string derive{ast::spelling(name.kind)};
        if (!derives.insert(derive).second)
          report(structure.ast->span, "duplicate derive '" + derive + "'");
        structure.derives.insert(derive);
      }
    if ((structure.derives.find("Copy") != structure.derives.end()) &&
        structure.derives.find("Clone") == structure.derives.end())
      report(structure.ast->span, "Copy requires an explicit Clone derive");
    if ((structure.derives.find("Eq") != structure.derives.end()) &&
        structure.derives.find("PartialEq") == structure.derives.end())
      report(structure.ast->span, "Eq requires an explicit PartialEq derive");
    for (const auto &field : structure.ast->fields) {
      record_symbol(field.id, SymbolKind::Field, field.name.text,
                    structure.ast->id);
      if (!fields.insert(field.name.text).second)
        report(field.span, "duplicate field '" + field.name.text + "'");
      structure.fields[field.name.text] = resolve_type(field.type, name);
      record_symbol_type(field.id, structure.fields[field.name.text]);
      structure.field_symbols[field.name.text] = symbol_for(field.id);
    }
    StructSemantics declaration{symbol_for(structure.ast->id), {}};
    for (const auto &field : structure.ast->fields)
      declaration.fields.push_back(symbol_for(field.id));
    result_.structures.push_back(std::move(declaration));
  }
}

} // namespace rx::semantic_detail
