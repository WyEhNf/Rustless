#pragma once

#include <cstddef>
#include <iosfwd>
#include <string>
#include <vector>

namespace rx {

struct Span {
  std::size_t begin{};
  std::size_t end{};
  std::size_t line{1};
  std::size_t column{};
};

enum class Severity { Error, Warning };

struct Diagnostic {
  Severity severity{Severity::Error};
  Span span{};
  std::string message;
};

class DiagnosticEngine {
public:
  // Diagnostics accumulate so one run can report independent errors together.
  explicit DiagnosticEngine(std::string file = {});

  void error(Span span, std::string message);
  void warning(Span span, std::string message);
  [[nodiscard]] bool has_errors() const;
  [[nodiscard]] std::size_t error_count() const;
  [[nodiscard]] const std::vector<Diagnostic> &diagnostics() const;
  void print(std::ostream &out) const;

private:
  std::string file_;
  std::vector<Diagnostic> diagnostics_;
};

} // namespace rx
