#include "llvm/lowering.hpp"

namespace rx::ir {

Value FunctionLowering::vector_push(Value receiver, Value element, const SemanticTypePtr &result) {
  const auto layout = module_.bridge.target().vector();
  const auto stride = module_.bridge.layout(receiver.type->element());
  const auto descriptor = address(receiver);
  const auto data_slot = offset(descriptor, layout.data_offset);
  const auto length_slot = offset(descriptor, layout.length_offset);
  const auto capacity_slot = offset(descriptor, layout.capacity_offset);
  const auto length = builder_.load(Type::I32, length_slot);
  const auto capacity = builder_.load(Type::I32, capacity_slot);
  const auto grow = builder_.create_block("vec.grow");
  const auto append = builder_.create_block("vec.append");
  builder_.cond_branch(builder_.compare(Predicate::Eq, length, capacity), grow, append);
  builder_.select(grow);
  const auto first = builder_.create_block("vec.first");
  const auto twice = builder_.create_block("vec.double");
  const auto allocate = builder_.create_block("vec.allocate");
  const auto zero = Operand::constant(Type::I32, 0);
  builder_.cond_branch(builder_.compare(Predicate::Eq, capacity, zero), first, twice);
  builder_.select(first);
  builder_.branch(allocate);
  builder_.select(twice);
  const auto doubled = builder_.binary(BinaryOp::Mul, capacity, Operand::constant(Type::I32, 2));
  builder_.branch(allocate);
  builder_.select(allocate);
  const auto new_capacity =
      builder_.phi(Type::I32, {{Operand::constant(Type::I32, 4), first}, {doubled, twice}});
  const auto bytes =
      builder_.binary(BinaryOp::Mul, new_capacity, Operand::constant(Type::I32, stride.bytes));
  const auto data =
      builder_
          .call(Type::Ptr, "__rx_alloc", {bytes, Operand::constant(Type::I32, stride.alignment)})
          .value();
  const auto copying = builder_.create_block("vec.copy");
  const auto copied = builder_.create_block("vec.copied");
  builder_.cond_branch(builder_.compare(Predicate::Ne, length, zero), copying, copied);
  builder_.select(copying);
  const auto old_data = builder_.load(Type::Ptr, data_slot);
  const auto used_bytes =
      builder_.binary(BinaryOp::Mul, length, Operand::constant(Type::I32, stride.bytes));
  builder_.call(Type::Ptr, "memmove", {data, old_data, used_bytes});
  builder_.branch(copied);
  builder_.select(copied);
  builder_.store(data, data_slot);
  builder_.store(new_capacity, capacity_slot);
  builder_.branch(append);
  builder_.select(append);
  const auto current_data = builder_.load(Type::Ptr, data_slot);
  const auto element_offset =
      builder_.binary(BinaryOp::Mul, length, Operand::constant(Type::I32, stride.bytes));
  write(receiver.type->element(), offset(current_data, element_offset), std::move(element));
  const auto new_length = builder_.binary(BinaryOp::Add, length, Operand::constant(Type::I32, 1));
  builder_.store(new_length, length_slot);
  return unit(result);
}

Value FunctionLowering::vector_remove(Value receiver, Value index, const SemanticTypePtr &result) {
  const auto layout = module_.bridge.target().vector();
  const auto descriptor = address(receiver);
  const auto length_slot = offset(descriptor, layout.length_offset);
  const auto length = builder_.load(Type::I32, length_slot);
  const auto data = builder_.load(Type::Ptr, offset(descriptor, layout.data_offset));
  const auto position = scalar(index);
  const auto stride = Operand::constant(Type::I32, module_.bridge.layout(result).bytes);
  const auto bytes = builder_.binary(BinaryOp::Mul, position, stride);
  const auto item = offset(data, bytes);
  auto value = freeze(read(result, item));
  const auto new_length = builder_.binary(BinaryOp::Sub, length, Operand::constant(Type::I32, 1));
  const auto remaining = builder_.binary(BinaryOp::Sub, new_length, position);
  const auto remaining_bytes = builder_.binary(BinaryOp::Mul, remaining, stride);
  builder_.call(Type::Ptr, "memmove", {item, offset(item, stride), remaining_bytes});
  builder_.store(new_length, length_slot);
  return value;
}

}
