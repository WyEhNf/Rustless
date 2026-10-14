#include "detail/analyzer.hpp"

namespace rx::semantic_detail {

SymbolId Analyzer::symbol_for(ast::NodeId node){
  auto [entry, inserted] =
      declaration_symbols_.try_emplace(node, next_symbol_);
  if (inserted)
    ++next_symbol_.value;
  return entry->second;
}

void Analyzer::record_symbol(ast::NodeId node, SymbolKind kind, std::string name,
                   std::optional<ast::NodeId> owner){
  auto id = symbol_for(node);
  std::optional<SymbolId> parent;
  if (owner)
    parent = symbol_for(*owner);
  if (recorded_symbols_.insert(id).second)
    result_.symbols.push_back({id, kind, std::move(name), parent, node});
}

void Analyzer::record_symbol_type(ast::NodeId declaration,
                        const SemanticTypePtr &type){
  symbol_types_[symbol_for(declaration)] = type;
}

void Analyzer::collect_items(){
  std::unordered_set<std::string> type_names;
  std::unordered_set<std::string> value_names;
  for (const auto &item : crate_.items) {
    ast::visit(item, [&](const ast::Node &, const auto &value) {
      using Payload = std::decay_t<decltype(value)>;
      if constexpr (std::is_same_v<Payload, ast::Struct>) {
        record_symbol(value.id, SymbolKind::Struct, value.name.text);
        if (builtin_type(value.name.text))
          report(value.span, "the builtin type name '" + value.name.text +
                                 "' is protected");
        if (!type_names.insert(value.name.text).second)
          report(value.span, "duplicate type name '" + value.name.text + "'");
        structures_[value.name.text].ast = &value;
      } else if constexpr (std::is_same_v<Payload, ast::Function>) {
        record_symbol(value.id, SymbolKind::Function, value.name.text);
        if (protected_builtin_value(value.name.text))
          report(value.span, "the builtin value name '" + value.name.text +
                                 "' is protected");
        if (!value_names.insert(value.name.text).second)
          report(value.span,
                 "duplicate value name '" + value.name.text + "'");
        functions_[value.name.text].ast = &value;
      } else if constexpr (std::is_same_v<Payload, ast::Constant>) {
        record_symbol(value.id, SymbolKind::Constant, value.name.text);
        if (protected_builtin_value(value.name.text))
          report(value.span, "the builtin value name '" + value.name.text +
                                 "' is protected");
        if (!value_names.insert(value.name.text).second)
          report(value.span,
                 "duplicate value name '" + value.name.text + "'");
        constants_[value.name.text].ast = &value;
      } else if constexpr (std::is_same_v<Payload, ast::Impl>) {
        impls_.push_back(&value);
      } else {
        static_assert(std::is_same_v<Payload, ast::Use>,
                      "unhandled AST declaration");
        (void)value;
      }
    });
  }
}

bool Analyzer::builtin_type(std::string_view name){
  return name == "bool" || name == "i32" || name == "u32" ||
         name == "isize" || name == "usize" || name == "Box" ||
         name == "Vec" || name == "Copy" || name == "Clone" ||
         name == "PartialEq" || name == "Eq";
}

bool Analyzer::protected_builtin_value(std::string_view name){
  return name == "getInt" || name == "printInt" || name == "printlnInt";
}

} // namespace rx::semantic_detail
