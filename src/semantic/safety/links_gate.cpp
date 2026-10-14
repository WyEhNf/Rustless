#include "checks.hpp"

namespace rx {

void SafeGate::validate_links() const {
  std::unordered_set<const SemanticType *> checked_types;
  std::unordered_set<const SemanticType *> active_types;
  std::function<void(const SemanticTypePtr &)> require_type;
  require_type = [&](const SemanticTypePtr &type) {
    if (!type || type->kind() == SemanticType::Kind::Error)
      throw std::invalid_argument("checked semantic fact has no valid type");
    if (checked_types.find(type.get()) != checked_types.end())
      return;
    if (!active_types.insert(type.get()).second)
      throw std::invalid_argument("cyclic semantic type object graph");
    rx::visit(*type, [&](const SemanticType &, const auto &payload) {
      using Payload = std::decay_t<decltype(payload)>;
      if constexpr (std::is_same_v<Payload, BoxType> ||
                    std::is_same_v<Payload, VecType> ||
                    std::is_same_v<Payload, ArrayType> ||
                    std::is_same_v<Payload, RefType>)
        require_type(payload.element);
      if constexpr (std::is_same_v<Payload, BuiltinFunctionType>)
        if (payload.owner)
          require_type(payload.owner);
      if constexpr (std::is_same_v<Payload, StructType> ||
                    std::is_same_v<Payload, FunctionType>) {
        const auto &record = symbol(payload.symbol);
        bool correct_kind;
        if constexpr (std::is_same_v<Payload, StructType>) {
          correct_kind = record.kind == SymbolKind::Struct;
        } else {
          correct_kind = record.kind == SymbolKind::Function ||
                         record.kind == SymbolKind::AssociatedFunction;
        }
        if (!correct_kind || record.name != payload.name)
          throw std::invalid_argument(
              "semantic nominal type identity mismatch");
      }
    });
    active_types.erase(type.get());
    checked_types.insert(type.get());
  };
  for (const auto &[_, fact] : symbols_) {
    if (fact->owner)
      (void)symbol(*fact->owner);
    switch (fact->kind) {
    case SymbolKind::Struct:
      (void)structure(fact->id);
      break;
    case SymbolKind::Function:
    case SymbolKind::AssociatedFunction:
      (void)function(fact->id);
      break;
    case SymbolKind::Constant:
    case SymbolKind::AssociatedConstant:
      (void)constant(fact->id);
      require_type(symbol_type(fact->id));
      break;
    default:
      require_type(symbol_type(fact->id));
      break;
    }
  }
  for (const auto &[_, fact] : expressions_) {
    require_type(fact->type);
    if (fact->symbol)
      (void)symbol(*fact->symbol);
    for (const auto &adjustment : fact->adjustments)
      require_type(adjustment.target);
    if (fact->callable)
      std::visit(ast::Overloaded{[&](const FunctionTarget &target) {
                                   (void)function(target.symbol);
                                 },
                                 [&](const BuiltinTarget &target) {
                                   (void)builtin_name(target.builtin);
                                 }},
                 *fact->callable);
    for (const auto *operation : {&fact->binary, &fact->compound})
      if (*operation) {
        require_type((*operation)->result);
        for (const auto *operand :
             {&(*operation)->left, &(*operation)->right}) {
          (void)expression(operand->node);
          require_type(operand->effective_type);
          for (const auto &adjustment : operand->adjustments)
            require_type(adjustment.target);
        }
      }
    if (fact->unary) {
      require_type(fact->unary->result);
      require_type(fact->unary->operand.effective_type);
      (void)expression(fact->unary->operand.node);
      for (const auto &adjustment : fact->unary->operand.adjustments)
        require_type(adjustment.target);
    }
    if (fact->receiver) {
      require_type(fact->receiver->effective_type);
      (void)expression(fact->receiver->node);
      for (const auto &adjustment : fact->receiver->adjustments)
        require_type(adjustment.target);
    }
  }
  for (const auto &[_, fact] : types_)
    require_type(fact->type);
  for (const auto &[id, fact] : symbol_types_) {
    (void)symbol(id);
    require_type(fact->type);
  }
  for (const auto &[id, fact] : functions_) {
    auto kind = symbol(id).kind;
    if (kind != SymbolKind::Function && kind != SymbolKind::AssociatedFunction)
      throw std::invalid_argument(
          "function fact links to a non-function symbol");
    require_type(fact->result);
    auto parameter = [&](const ParameterSemantics &value, SymbolKind expected) {
      const auto &record = symbol(value.symbol);
      if (record.kind != expected || record.owner != id)
        throw std::invalid_argument("parameter owner or kind mismatch");
      require_type(value.type);
    };
    if (fact->receiver)
      parameter(*fact->receiver, SymbolKind::Receiver);
    for (const auto &value : fact->parameters)
      parameter(value, SymbolKind::Parameter);
  }
  for (const auto &[id, fact] : structures_) {
    if (symbol(id).kind != SymbolKind::Struct)
      throw std::invalid_argument("struct fact links to a non-struct symbol");
    std::unordered_set<SymbolId> seen;
    for (auto field : fact->fields) {
      const auto &record = symbol(field);
      if (record.kind != SymbolKind::Field || record.owner != id ||
          !seen.insert(field).second)
        throw std::invalid_argument("field owner, kind or identity mismatch");
      require_type(symbol_type(field));
    }
  }
  for (const auto &[id, _] : constants_) {
    auto kind = symbol(id).kind;
    if (kind != SymbolKind::Constant && kind != SymbolKind::AssociatedConstant)
      throw std::invalid_argument(
          "constant fact links to a non-constant symbol");
  }
}

} // namespace rx
