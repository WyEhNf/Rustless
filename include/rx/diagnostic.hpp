#pragma once

#include <cstddef>
#include <cstdint>
#include <iosfwd>
#include <string>
#include <vector>

namespace rx {

struct FileId {
  std::uint32_t value{};

  constexpr bool operator==(const FileId &other) const { return value == other.value; }
  constexpr bool operator!=(const FileId &other) const { return value != other.value; }
  constexpr bool operator<(const FileId &other) const { return value < other.value; }
  constexpr bool operator<=(const FileId &other) const { return value <= other.value; }
  constexpr bool operator>(const FileId &other) const { return value > other.value; }
  constexpr bool operator>=(const FileId &other) const { return value >= other.value; }
};

struct Span {
  std::size_t begin{};
  std::size_t end{};
  std::size_t line{1};
  std::size_t column{};
  FileId file{};
};

enum class Severity { Error, Warning };

struct Diagnostic {
  Severity severity{Severity::Error};
  Span span{};
  std::string message;
};

class DiagnosticEngine {
public:
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

}
