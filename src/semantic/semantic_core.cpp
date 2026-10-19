#include "semantic/analyzer.hpp"

#include <algorithm>
#include <utility>

namespace rx::semantic_detail {

Analyzer::Analyzer(const ast::Crate &crate, DiagnosticEngine &diagnostics)
    : crate_(crate), diagnostics_(diagnostics) {}

SemanticResult Analyzer::run() {
  result_.source_crate = &crate_;

  collect_items();
  resolve_declarations();

  for (const auto &[_, function] : functions_)
    callable_functions_.emplace(symbol_for(function.ast->id), &function);
  for (const auto &[_, function] : associated_functions_)
    callable_functions_.emplace(symbol_for(function.ast->id), &function);

  validate_constants();
  validate_layouts_and_derives();
  validate_entry();
  check_functions();
  result_.summary = {functions_.size(), structures_.size(), constants_.size()};
  std::sort(result_.symbols.begin(), result_.symbols.end(),
            [](const SymbolRecord &left, const SymbolRecord &right) {
              return left.id < right.id;
            });

  for (auto &[_, fact] : expression_facts_)
    result_.expressions.push_back(std::move(fact));

  std::sort(result_.functions.begin(), result_.functions.end(),
            [](const auto &left, const auto &right) {
              return left.symbol < right.symbol;
            });
  std::sort(result_.structures.begin(), result_.structures.end(),
            [](const auto &left, const auto &right) {
              return left.symbol < right.symbol;
            });

  for (auto &[node, type] : resolved_types_)
    result_.resolved_types.push_back({node, std::move(type)});
  for (auto &[symbol, type] : symbol_types_)
    result_.symbol_types.push_back({symbol, std::move(type)});
  for (const auto &[_, constant] : constants_)
    if (constant.state == ConstantInfo::State::Done)
      result_.constants.push_back({symbol_for(constant.ast->id),
                                   static_cast<std::uint32_t>(constant.value)});
  for (const auto &[_, constant] : associated_constants_)
    if (constant.state == ConstantInfo::State::Done)
      result_.constants.push_back({symbol_for(constant.ast->id),
                                   static_cast<std::uint32_t>(constant.value)});
  std::sort(result_.constants.begin(), result_.constants.end(),
            [](const auto &left, const auto &right) {
              return left.symbol < right.symbol;
            });
  result_.success = !diagnostics_.has_errors();

  if (result_.success)
    return SemanticResult::publish(crate_, std::move(result_));
  return SemanticResult::rejected(crate_, std::move(result_));
}

void Analyzer::report(Span span, std::string message) {
  diagnostics_.error(span, std::move(message));
}

} // namespace rx::semantic_detail

namespace rx {

SemanticResult analyze(const ast::Crate &crate, DiagnosticEngine &diagnostics) {
  return semantic_detail::Analyzer(crate, diagnostics).run();
}

} // namespace rx
