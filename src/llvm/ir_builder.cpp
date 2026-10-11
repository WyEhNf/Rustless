#include "rx/ir_builder.hpp"
#include <algorithm>
#include <limits>
#include <stdexcept>

namespace rx::ir {
Builder::Builder(Function &function) : function_(function) {
  std::uint32_t maximum = 0;
  for (const auto &parameter : function.parameters) maximum = std::max(maximum, parameter.id.value);
  for (const auto &block : function.blocks) {
    next_block_ = std::max(next_block_, std::uint64_t{block.id.value} + 1);
    for (const auto &node : block.instructions)
      if (auto result = definition(node)) maximum = std::max(maximum, result->id.value);
  }
  if (maximum == std::numeric_limits<std::uint32_t>::max()) function_.next_value = {};
  else if (function_.next_value.value) function_.next_value.value = std::max(function_.next_value.value, maximum + 1);
}
BlockId Builder::create_block(std::string name) {
  if (next_block_ > std::numeric_limits<std::uint32_t>::max()) throw std::overflow_error("block identities exhausted");
  BlockId id{static_cast<std::uint32_t>(next_block_++)};
  function_.blocks.push_back({id, std::move(name), {}});
  return id;
}
void Builder::select(BlockId id) {
  for (const auto &block : function_.blocks)
    if (block.id == id) { current_ = id; return; }
  throw std::invalid_argument("selected block does not belong to the function");
}
Block &Builder::current() {
  if (!current_) throw std::logic_error("no insertion block selected");
  for (auto &block : function_.blocks) if (block.id == *current_) return block;
  throw std::logic_error("selected block was removed");
}
Result Builder::fresh(Type type) {
  if (!is_value_type(type)) throw std::invalid_argument("SSA results require a value type");
  if (!function_.next_value.value) throw std::overflow_error("SSA identities exhausted");
  auto id = function_.next_value;
  ++function_.next_value.value;
  return {id, type};
}
void Builder::append(Instruction::Data data) {
  Instruction node{std::move(data), origin_};
  auto errors = verify_instruction(node);
  if (!errors.empty()) throw std::invalid_argument(errors.front());
  auto &block = current();
  if (!block.instructions.empty() && is_terminator(block.instructions.back()))
    throw std::logic_error("cannot append after a terminator");
  if (std::holds_alternative<Phi>(node.data) && std::any_of(block.instructions.begin(), block.instructions.end(),
      [](const Instruction &p) { return !std::holds_alternative<Phi>(p.data); }))
    throw std::logic_error("phi must precede ordinary instructions");
  block.instructions.push_back(std::move(node));
}
} // namespace rx::ir
