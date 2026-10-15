#include "lowering.hpp"

namespace rx::ir {

Value FunctionLowering::emit(const ast::Expr &node, const ast::ArrayExpr &payload) {
  const auto result_type = type(node);
  const auto output = storage(result_type);
  const auto element = result_type->element();
  const auto stride = module_.bridge.layout(element).bytes;
  for (std::size_t index = 0; index < payload.elements.size(); ++index) {
    auto value = expression(*payload.elements[index]);
    if (builder_.terminated())
      return unit(result_type);
    const auto destination = offset(output, static_cast<std::uint32_t>(index) * stride);
    write(element, destination, convert(std::move(value), element));
  }
  return {result_type, TemporaryValue{output}};
}

Value FunctionLowering::emit(const ast::Expr &node, const ast::RepeatArrayExpr &payload) {
  const auto result_type = type(node);
  auto value = expression(*payload.element);
  if (builder_.terminated())
    return unit(result_type);
  const auto output = storage(result_type);
  const auto element = result_type->element();
  const auto stride = Operand::constant(Type::I32, module_.bridge.layout(element).bytes);
  const auto count = Operand::constant(Type::I32, static_cast<std::uint32_t>(result_type->length()));
  value = convert(std::move(value), element);
  each(count, [&](Operand index) {
    auto bytes = builder_.binary(BinaryOp::Mul, index, stride);
    write(element, offset(output, bytes), value);
  });
  return {result_type, TemporaryValue{output}};
}

Value FunctionLowering::emit(const ast::Expr &node, const ast::StructExpr &payload) {
  const auto result_type = type(node);
  const auto output = storage(result_type);
  const auto &layout = module_.bridge.structure(result_type->symbol());
  for (const auto &initializer : payload.fields) {
    auto value = expression(*initializer.value);
    if (builder_.terminated())
      return unit(result_type);
    for (const auto &field : layout.fields) {
      if (facts().symbol(field.symbol).name != initializer.name.text)
        continue;
      const auto &field_type = facts().symbol_type(field.symbol);
      write(field_type, offset(output, field.offset), convert(std::move(value), field_type));
      break;
    }
  }
  return {result_type, TemporaryValue{output}};
}

void FunctionLowering::each(Operand count, const std::function<void(Operand)> &body) {
  const auto index_slot = builder_.allocate_entry(4, 4);
  builder_.store(Operand::constant(Type::I32, 0), index_slot);
  const auto head = builder_.create_block("each.head");
  const auto element = builder_.create_block("each.body");
  const auto exit = builder_.create_block("each.end");
  builder_.branch(head);
  builder_.select(head);
  const auto index = builder_.load(Type::I32, index_slot);
  const auto condition = builder_.compare(Predicate::ULt, index, count);
  builder_.cond_branch(condition, element, exit);
  builder_.select(element);
  body(index);
  if (!builder_.terminated()) {
    const auto next = builder_.binary(BinaryOp::Add, index, Operand::constant(Type::I32, 1));
    builder_.store(next, index_slot);
    builder_.branch(head);
  }
  builder_.select(exit);
}

}
