#include "lowering.hpp"

namespace rx::ir {

void FunctionLowering::helper_body(const HelperRequest &request) {
  const auto left = parameter(Type::Ptr, "destination_or_left");
  const auto right = parameter(Type::Ptr, "source_or_right");
  if (request.kind == HelperKind::Clone)
    clone_body(request.type, left, right);
  else
    equality_body(request.type, left, right);
}

void FunctionLowering::clone_body(const SemanticTypePtr &type, Operand destination,
                                 Operand source) {
  auto clone_child = [&](const SemanticTypePtr &child, Operand output, Operand input) {
    const auto name = module_.helper(HelperKind::Clone, child);
    builder_.call(Type::Void, name, {output, input});
  };
  switch (type->kind()) {
  case SemanticType::Kind::Struct:
    for (const auto &field : module_.bridge.structure(type->symbol()).fields)
      clone_child(facts().symbol_type(field.symbol), offset(destination, field.offset),
                  offset(source, field.offset));
    break;
  case SemanticType::Kind::Array: {
    const auto stride = Operand::constant(Type::I32, module_.bridge.layout(type->element()).bytes);
    const auto count = Operand::constant(Type::I32, static_cast<std::uint32_t>(type->length()));
    each(count, [&](Operand index) {
      const auto bytes = builder_.binary(BinaryOp::Mul, index, stride);
      clone_child(type->element(), offset(destination, bytes), offset(source, bytes));
    });
    break;
  }
  case SemanticType::Kind::Box: {
    const auto layout = module_.bridge.layout(type->element());
    const auto pointer = builder_.call(Type::Ptr, "__rx_alloc",
        {Operand::constant(Type::I32, layout.bytes),
         Operand::constant(Type::I32, layout.alignment)}).value();
    builder_.store(pointer, destination);
    clone_child(type->element(), pointer, builder_.load(Type::Ptr, source));
    break;
  }
  case SemanticType::Kind::Vec: {
    const auto vector = module_.bridge.target().vector();
    const auto length = builder_.load(Type::I32, offset(source, vector.length_offset));
    builder_.store(length, offset(destination, vector.length_offset));
    builder_.store(length, offset(destination, vector.capacity_offset));
    builder_.store(Operand::constant(Type::Ptr, 0), destination);
    const auto nonempty = builder_.create_block("clone.nonempty");
    const auto end = builder_.create_block("clone.end");
    const auto condition = builder_.compare(Predicate::Ne, length, Operand::constant(Type::I32, 0));
    builder_.cond_branch(condition, nonempty, end);
    builder_.select(nonempty);
    const auto layout = module_.bridge.layout(type->element());
    const auto stride = Operand::constant(Type::I32, layout.bytes);
    const auto bytes = builder_.binary(BinaryOp::Mul, length, stride);
    const auto data = builder_.call(Type::Ptr, "__rx_alloc",
        {bytes, Operand::constant(Type::I32, layout.alignment)}).value();
    builder_.store(data, destination);
    const auto input = builder_.load(Type::Ptr, source);
    each(length, [&](Operand index) {
      const auto element_offset = builder_.binary(BinaryOp::Mul, index, stride);
      clone_child(type->element(), offset(data, element_offset), offset(input, element_offset));
    });
    builder_.branch(end);
    builder_.select(end);
    break;
  }
  default:
    write(type, destination, read(type, source));
    break;
  }
  builder_.ret();
}

}
