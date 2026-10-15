#pragma once

#include "rx/ast/node.hpp"

namespace rx::ir {
struct ValueTag;
struct BlockTag;
using ValueId = ast::Id<ValueTag>;
using BlockId = ast::Id<BlockTag>;
struct Origin {
  ast::NodeId node;
  Span span;
};
inline Origin origin(const ast::Node &node) { return {node.id, node.span}; }
}
