#include "detail/analyzer.hpp"

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <functional>
#include <iterator>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <sstream>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

namespace rx::semantic_detail {



Analyzer::Analyzer(const ast::Crate &crate, DiagnosticEngine &diagnostics)
    : crate_(crate), diagnostics_(diagnostics){}

SemanticResult Analyzer::run(){
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
  result_.summary = {functions_.size(), structures_.size(),
                     constants_.size()};
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
      result_.constants.push_back(
          {symbol_for(constant.ast->id),
           static_cast<std::uint32_t>(constant.value)});
  for (const auto &[_, constant] : associated_constants_)
    if (constant.state == ConstantInfo::State::Done)
      result_.constants.push_back(
          {symbol_for(constant.ast->id),
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

void Analyzer::report(Span span, std::string message){
  diagnostics_.error(span, std::move(message));
}

} // namespace rx::semantic_detail

namespace rx {

std::string_view symbol_kind_name(SymbolKind kind) {
  constexpr std::array names{"struct",
                             "function",
                             "constant",
                             "associated-function",
                             "associated-constant",
                             "field",
                             "parameter",
                             "receiver",
                             "local"};
  return names.at(static_cast<std::size_t>(kind));
}
SemanticResult analyze(const ast::Crate &crate, DiagnosticEngine &diagnostics) {
  return semantic_detail::Analyzer(crate, diagnostics).run();
}

std::string dump_semantic(const SemanticResult &checked) {
  const auto &result = checked.data();
  std::ostringstream out;
  out << "Semantic\n"
      << "  Summary functions=" << result.summary.functions
      << " structures=" << result.summary.structures
      << " constants=" << result.summary.constants << '\n';
  out << "  Symbols\n";
  for (const auto &symbol : result.symbols) {
    out << "    #" << symbol.id << ' ' << symbol_kind_name(symbol.kind) << ' '
        << symbol.name << " declaration=#" << symbol.declaration;
    if (symbol.owner)
      out << " owner=#" << *symbol.owner;
    out << '\n';
  }
  out << "  Expressions\n";
  for (const auto &expression : result.expressions) {
    out << "    #" << expression.node << " type=" << type_name(expression.type);
    if (expression.is_place) {
      out << " place";
      if (expression.is_mutable)
        out << " mutable";
      else
        out << " immutable";
    } else {
      out << " value";
    }
    if (expression.access_locked)
      out << " access-locked";
    if (expression.symbol)
      out << " symbol=#" << *expression.symbol;
    if (!expression.target.empty())
      out << " target=" << expression.target;
    if (!expression.adjustments.empty()) {
      out << " adjustments=[";
      for (std::size_t index = 0; index < expression.adjustments.size();
           ++index) {
        if (index)
          out << ", ";
        const auto &adjustment = expression.adjustments[index];
        switch (adjustment.kind) {
        case Adjustment::Kind::Dereference:
          out << "deref";
          break;
        case Adjustment::Kind::BorrowShared:
          out << "borrow-shared";
          break;
        case Adjustment::Kind::BorrowMutable:
          out << "borrow-mutable";
          break;
        case Adjustment::Kind::MutToShared:
          out << "mut-to-shared";
          break;
        case Adjustment::Kind::NeverToAny:
          out << "never-to-any";
          break;
        }
        if (adjustment.target)
          out << "->" << type_name(adjustment.target);
      }
      out << ']';
    }
    out << '\n';
  }
  return out.str();
}

} // namespace rx
