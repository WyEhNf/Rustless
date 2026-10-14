#include "detail/analyzer.hpp"

namespace rx::semantic_detail {

void Analyzer::resolve_declarations() {

  resolve_impls();

  for (auto &[_, constant] : constants_)
    resolve_constant_type(constant);
  for (auto &[_, constant] : associated_constants_)
    resolve_constant_type(constant);
  for (auto &[name, function] : associated_functions_)
    resolve_function(function, name, function.owner);
  resolve_structures();
  for (auto &[name, function] : functions_)
    resolve_function(function, name, {});
}

void Analyzer::resolve_impls() {
  for (const auto *implementation : impls_) {
    auto target = resolve_type(implementation->target);
    if (target->kind() != SemanticType::Kind::Struct ||
        structures_.find(target->name()) == structures_.end()) {
      report(implementation->span,
             "an inherent impl target must be a user-defined struct");
      continue;
    }
    const std::string owner = target->name();
    const auto owner_symbol = structures_.at(owner).ast->id;
    std::unordered_set<std::string> associated_names;
    for (const auto &member : implementation->members) {
      ast::visit(member, [&](const ast::Node &, const auto &declaration) {
        using Payload = std::decay_t<decltype(declaration)>;
        const auto &name = declaration.name.text;
        auto key = owner + "::" + name;
        auto kind = SymbolKind::AssociatedConstant;
        if constexpr (std::is_same_v<Payload, ast::Function>)
          kind = SymbolKind::AssociatedFunction;
        record_symbol(declaration.id, kind, key, owner_symbol);
        if (!associated_names.insert(name).second ||
            (associated_functions_.find(key) != associated_functions_.end()) ||
            (associated_constants_.find(key) != associated_constants_.end()))
          report(declaration.span, "duplicate associated item '" + name + "'");
        if constexpr (std::is_same_v<Payload, ast::Function>) {
          associated_functions_[key].ast = &declaration;
          associated_functions_[key].owner = owner;
        } else {
          static_assert(std::is_same_v<Payload, ast::Constant>);
          associated_constants_[key].ast = &declaration;
          associated_constants_[key].owner = owner;
        }
      });
    }
  }
}

} // namespace rx::semantic_detail
