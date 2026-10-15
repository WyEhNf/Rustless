#include "rx/llvm/builder.hpp"
#include <algorithm>
#include <limits>
#include <stdexcept>

namespace rx::ir {
Builder::Builder(Function &function) : function_(function) {
  std::uint32_t maximum = 0;
  for (const auto &parameter : function.parameters) {
    maximum = std::max(maximum, parameter.id.value);
  }
  for (const auto &block : function.blocks) {
    next_block_ = std::max(next_block_, std::uint64_t{block.id.value} + 1);
    for (const auto &node : block.instructions) {
      if (auto result = definition(node)) {
        maximum = std::max(maximum, result->id.value);
      }
    }
  }
  if (maximum == std::numeric_limits<std::uint32_t>::max()) {
    function_.next_value = {};
  } else if (function_.next_value.value) {
    function_.next_value.value = std::max(function_.next_value.value, maximum + 1);
  }
}
BlockId Builder::create_block(std::string name) {
  if (next_block_ > std::numeric_limits<std::uint32_t>::max()) {
    throw std::overflow_error("block identities exhausted");
  }
  BlockId id{static_cast<std::uint32_t>(next_block_++)};
  function_.blocks.push_back({id, std::move(name), {}});
  return id;
}
void Builder::select(BlockId id) {
  for (const auto &block : function_.blocks) {
    if (block.id == id) {
      current_ = id;
      return;
    }
  }
  throw std::invalid_argument("selected block does not belong to the function");
}
Block &Builder::current() {
  if (!current_) {
    throw std::logic_error("no insertion block selected");
  }
  for (auto &block : function_.blocks) {
    if (block.id == *current_) {
      return block;
    }
  }
  throw std::logic_error("selected block was removed");
}
Result Builder::fresh(Type type) {
  if (!is_value_type(type)) {
    throw std::invalid_argument("SSA results require a value type");
  }
  if (!function_.next_value.value) {
    throw std::overflow_error("SSA identities exhausted");
  }
  auto id = function_.next_value;
  ++function_.next_value.value;
  return {id, type};
}
bool Builder::terminated() const {
  for (const auto &block : function_.blocks) {
    if (block.id == current_) {
      return !block.instructions.empty() && is_terminator(block.instructions.back());
    }
  }
  return false;
}
Operand Builder::allocate_entry(std::uint32_t bytes, std::uint32_t alignment) {
  auto result = fresh(Type::Ptr);
  auto &instructions = function_.blocks.front().instructions;
  auto insertion = instructions.begin();
  while (insertion != instructions.end() && std::holds_alternative<Alloca>(insertion->data))
    ++insertion;
  instructions.insert(insertion, Instruction{Alloca{result.id, std::max(bytes, 1u), alignment}, origin_});
  return Operand::local(Type::Ptr, result.id);
}
void Builder::append(Instruction::Data data) {
  Instruction node{std::move(data), origin_};
  auto errors = verify_instruction(node);
  if (!errors.empty()) {
    throw std::invalid_argument(errors.front());
  }
  auto &block = current();
  if (!block.instructions.empty() && is_terminator(block.instructions.back())) {
    throw std::logic_error("cannot append after a terminator");
  }
  if (std::holds_alternative<Phi>(node.data) &&
      std::any_of(
          block.instructions.begin(), block.instructions.end(), [](const Instruction &payload) {
            return !std::holds_alternative<Phi>(payload.data);
          })) {
    throw std::logic_error("phi must precede ordinary instructions");
  }
  block.instructions.push_back(std::move(node));
}
Operand Builder::allocate(std::uint32_t bytes, std::uint32_t alignment) {
  if (current().id != function_.blocks.front().id) {
    throw std::logic_error("fixed allocation requires the entry block");
  }
  auto result = fresh(Type::Ptr);
  append(Alloca{result.id, bytes, alignment});
  return Operand::local(result.type, result.id);
}
Operand Builder::load(Type type, Operand address, std::uint32_t alignment) {
  auto result = fresh(type);
  append(Load{result, std::move(address), alignment});
  return Operand::local(type, result.id);
}
void Builder::store(Operand value, Operand address, std::uint32_t alignment) {
  append(Store{std::move(value), std::move(address), alignment});
}
Operand Builder::gep(Operand base, Operand offset) {
  auto result = fresh(Type::Ptr);
  append(Gep{result.id, std::move(base), std::move(offset)});
  return Operand::local(result.type, result.id);
}
Operand Builder::binary(BinaryOp op, Operand left, Operand right) {
  auto result = fresh(left.type);
  append(Binary{result, op, std::move(left), std::move(right)});
  return Operand::local(result.type, result.id);
}
Operand Builder::compare(Predicate predicate, Operand left, Operand right) {
  auto result = fresh(Type::I1);
  append(Compare{result.id, predicate, std::move(left), std::move(right)});
  return Operand::local(result.type, result.id);
}
Operand Builder::cast(CastOp op, Type target, Operand operand) {
  auto result = fresh(target);
  append(Cast{result, op, std::move(operand)});
  return Operand::local(result.type, result.id);
}
std::optional<Operand> Builder::call(Type type, std::string callee,
                                     std::vector<Operand> arguments) {
  std::optional<Result> result;
  if (type != Type::Void) {
    result = fresh(type);
  }
  append(Call{result, std::move(callee), std::move(arguments)});
  if (!result) {
    return {};
  }
  return Operand::local(result->type, result->id);
}
Operand Builder::phi(Type type, std::vector<PhiIncoming> incoming) {
  auto result = fresh(type);
  append(Phi{result, std::move(incoming)});
  return Operand::local(type, result.id);
}
void Builder::branch(BlockId target) {
  append(Branch{target});
}
void Builder::cond_branch(Operand condition, BlockId consequent, BlockId alternative) {
  append(CondBranch{std::move(condition), consequent, alternative});
}
void Builder::ret(std::optional<Operand> value) {
  if ((!value && function_.result != Type::Void) || (value && value->type != function_.result)) {
    throw std::invalid_argument("return does not match the function result");
  }
  append(Return{std::move(value)});
}
void Builder::unreachable() {
  append(Unreachable{});
}
}
