#include "rx/semantic/analyzer.hpp"

namespace rx::semantic_detail {

std::string_view Analyzer::current_owner() const {
  if (current_function_)
    return current_function_->owner;
  return {};
}

void Analyzer::resolve_function(FunctionInfo &function, const std::string &,
                                std::string owner) {
  function.owner = std::move(owner);
  std::unordered_set<std::string> parameters;
  for (const auto &parameter : function.ast->signature.parameters) {
    if (!parameters.insert(parameter.binding.name.text).second)
      report(parameter.span,
             "duplicate parameter '" + parameter.binding.name.text + "'");
    function.parameters.push_back(resolve_type(parameter.type, function.owner));
  }
  function.result =
      resolve_type(function.ast->signature.return_annotation, function.owner);
}

void Analyzer::validate_entry() {
  auto found = functions_.find("main");
  if (found == functions_.end()) {
    report(crate_.span, "an executable requires a top-level main function");
    return;
  }
  const auto &main = found->second;
  if (!main.parameters.empty())
    report(main.ast->span, "main cannot have value parameters");
  if (main.ast->generics && !main.ast->generics->lifetimes.empty())
    report(main.ast->span, "main cannot have generic parameters");
  if (main.result->kind() != SemanticType::Kind::Unit)
    report(main.ast->span, "main must return unit");
}

void Analyzer::check_functions() {
  for (const auto &[_, function] : functions_)
    check_function(function);
  for (const auto &[_, function] : associated_functions_)
    check_function(function);
}

void Analyzer::check_function(const FunctionInfo &function) {
  FunctionSemantics signature{
      symbol_for(function.ast->id), {}, {}, function.result};

  scopes_.clear();
  scopes_.emplace_back();
  current_function_ = &function;
  if (function.ast->signature.receiver.has_value()) {
    if (function.owner.empty())
      report(function.ast->span, "self is only available in a method");
    auto self = structure_type(function.owner);
    if (ast::by_reference(*function.ast->signature.receiver))
      self = compound(SemanticType::Kind::Ref, self,
                      ast::is_mutable(*function.ast->signature.receiver));
    record_symbol(function.ast->signature.receiver->id, SymbolKind::Receiver,
                  "self", function.ast->id);
    record_symbol_type(function.ast->signature.receiver->id, self);
    signature.receiver = ParameterSemantics{
        symbol_for(function.ast->signature.receiver->id), self};
    scopes_.back()["self"] = {
        self, ast::is_mutable(*function.ast->signature.receiver),
        symbol_for(function.ast->signature.receiver->id)};
  }
  for (std::size_t index = 0; index < function.ast->signature.parameters.size();
       ++index) {
    const auto &parameter = function.ast->signature.parameters[index];
    if (protected_builtin_value(parameter.binding.name.text))
      report(parameter.span, "cannot shadow a protected builtin");
    record_symbol(parameter.id, SymbolKind::Parameter,
                  parameter.binding.name.text, function.ast->id);
    record_symbol_type(parameter.id, function.parameters[index]);
    signature.parameters.push_back(
        {symbol_for(parameter.id), function.parameters[index]});
    scopes_.back()[parameter.binding.name.text] = {
        function.parameters[index],
        parameter.binding.mutability == ast::Mutability::Mutable,
        symbol_for(parameter.id)};
  }
  auto body = check_block(*function.ast->body, function.result);
  if (!coercible(body.type, function.result))
    mismatch(function.ast->body->span, body.type, function.result);
  current_function_ = nullptr;
  result_.functions.push_back(std::move(signature));
}

} // namespace rx::semantic_detail
