#pragma once

#include "rx/ast/node.hpp"
#include <algorithm>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace rx::ast {
struct SourceFile {
  explicit SourceFile(std::string text) : text(std::move(text)), lines{0} {
    for (std::size_t offset = 0; offset < this->text.size(); ++offset) {
      if (this->text[offset] == '\n')
        lines.push_back(offset + 1);
      const auto byte = static_cast<unsigned char>(this->text[offset]);
      if ((byte & 0xc0) != 0x80)
        characters.push_back(offset);
    }
    characters.push_back(this->text.size());
  }
  std::string_view slice(Span span) const {
    if (span.begin > span.end || span.end > text.size())
      throw std::out_of_range("invalid source span");
    return std::string_view(text).substr(span.begin, span.end - span.begin);
  }
  std::pair<std::size_t, std::size_t> position(std::size_t offset) const {
    if (offset > text.size())
      throw std::out_of_range("invalid source offset");
    const auto next_line = std::upper_bound(lines.begin(), lines.end(), offset);
    const auto line_index = static_cast<std::size_t>(next_line - lines.begin() - 1);
    const auto line_begin =
        std::lower_bound(characters.begin(), characters.end(), lines[line_index]);
    const auto character = std::lower_bound(characters.begin(), characters.end(), offset);
    const auto column = static_cast<std::size_t>(character - line_begin);
    return {line_index + 1, column};
  }
  Span from_character_span(Span span) const {
    span.begin = characters.at(span.begin);
    span.end = characters.at(span.end);
    span.file = id;
    return span;
  }
  FileId id{1};
  std::string text;
  std::vector<std::size_t> lines;
  std::vector<std::size_t> characters;
};
}
