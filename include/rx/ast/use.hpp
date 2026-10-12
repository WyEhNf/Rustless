#pragma once

#include "rx/ast/names.hpp"
#include <optional>
#include <variant>
#include <vector>

namespace rx::ast {
enum class UseKeyword { Self, Super, Crate };
struct UseKeywordName {
  UseKeyword kind;
  Span span;
};
using UseSegment = std::variant<Identifier, UseKeywordName>;
enum class PathRoot { Relative, Absolute };
struct UsePath {
  Span span;
  PathRoot root{PathRoot::Relative};
  std::vector<UseSegment> segments;
};
struct UnderscoreAlias {
  Span span;
};
using UseAlias = std::variant<Identifier, UnderscoreAlias>;
struct NameImport {
  std::optional<UseAlias> alias;
};
struct GlobImport {};
struct UseTree;
struct GroupImport {
  std::vector<UseTree> children;
};
struct UseTree : Node {
  UsePath prefix;
  std::variant<NameImport, GlobImport, GroupImport> target;
};
struct Use : Node {
  UseTree tree;
};

inline std::string_view name_text(const UseSegment &segment) {
  return std::visit(
      Overloaded{
          [](const Identifier &name) -> std::string_view { return name.text; },
          [](const UseKeywordName &name) -> std::string_view {
            switch (name.kind) {
            case UseKeyword::Self:
              return "self";
            case UseKeyword::Super:
              return "super";
            case UseKeyword::Crate:
              return "crate";
            }
            throw std::logic_error("unknown import path keyword");
          }},
      segment);
}
} 
