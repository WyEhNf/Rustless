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
    for (std::size_t i = 0; i < this->text.size(); ++i) {
      if (this->text[i] == '\n') lines.push_back(i + 1);
      if ((static_cast<unsigned char>(this->text[i]) & 0xc0) != 0x80)
        characters.push_back(i);
    }
    characters.push_back(this->text.size());
  }
  std::string_view slice(Span span) const {
    if (span.begin > span.end || span.end > text.size())
      throw std::out_of_range("invalid source span");
    return std::string_view(text).substr(span.begin, span.end - span.begin);
  }
  std::pair<std::size_t, std::size_t> position(std::size_t offset) const {
    if (offset > text.size()) throw std::out_of_range("invalid source offset");
    auto line = std::upper_bound(lines.begin(), lines.end(), offset);
    auto index = static_cast<std::size_t>(line - lines.begin() - 1);
    auto begin = std::lower_bound(characters.begin(), characters.end(), lines[index]);
    auto end = std::lower_bound(characters.begin(), characters.end(), offset);
    return {index + 1, static_cast<std::size_t>(end - begin)};
  }
  Span from_character_span(Span span) const {
    // ANTLR indexes UTF-32 characters; syntax spans index owned UTF-8 bytes.
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
} // namespace rx::ast
