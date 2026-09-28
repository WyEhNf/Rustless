#pragma once

#include "rx/ast.hpp"
#include "rx/diagnostic.hpp"

#include <memory>
#include <string>
#include <string_view>

namespace rx {

struct FrontendResult {
  std::shared_ptr<ast::Crate> crate;
  // False means the generated parser rejected the token stream.
  bool syntax_ok{};
};

bool lex_source(std::string_view source, DiagnosticEngine &diagnostics);
bool parse_syntax(std::string_view source, std::string_view entry,
                  DiagnosticEngine &diagnostics);
FrontendResult parse_source(std::string_view source,
                            DiagnosticEngine &diagnostics);

} // namespace rx
