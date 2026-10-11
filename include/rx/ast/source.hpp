#pragma once

#include "rx/ast/node.hpp"
#include <algorithm>
#include <string>
#include <string_view>
#include <vector>

namespace rx::ast {
// One compilation unit owns the bytes from which its spans were constructed.
struct SourceFile {
  explicit SourceFile(std::string text) : text(std::move(text)), lines{0} {
    for (std::size_t i = 0; i < this->text.size(); ++i)
      if (this->text[i] == '\n') lines.push_back(i + 1);
  }
  std::string_view slice(Span span) const {
    return std::string_view(text).substr(span.begin, span.end - span.begin);
  }
  std::pair<std::size_t, std::size_t> position(std::size_t offset) const {
    auto line = std::upper_bound(lines.begin(), lines.end(), offset);
    auto index = static_cast<std::size_t>(line - lines.begin() - 1);
    return {index + 1, offset - lines[index]};
  }
  std::string text;
  std::vector<std::size_t> lines;
};
} // namespace rx::ast
