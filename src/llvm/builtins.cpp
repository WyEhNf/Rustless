#include "lowering.hpp"

namespace rx::ir {

Value FunctionLowering::builtin(BuiltinId id, std::vector<Value> arguments,
                              const SemanticTypePtr &result) {
  switch (id) {
  case BuiltinId::GetInt:
    return {result, ScalarValue{builder_.call(Type::I32, "getInt", {}).value()}};
  case BuiltinId::PrintInt:
    builder_.call(Type::Void, "printInt", {scalar(arguments[0])});
    return unit(result);
  case BuiltinId::PrintlnInt:
    builder_.call(Type::Void, "printlnInt", {scalar(arguments[0])});
    return unit(result);
  case BuiltinId::BoxNew: {
    const auto layout = module_.bridge.layout(result->element());
    const auto pointer = builder_.call(Type::Ptr, "__rx_alloc",
        {Operand::constant(Type::I32, layout.bytes),
         Operand::constant(Type::I32, layout.alignment)}).value();
    write(result->element(), pointer, arguments[0]);
    return {result, ScalarValue{pointer}};
  }
  case BuiltinId::VecNew: {
    const auto output = storage(result);
    const auto layout = module_.bridge.target().vector();
    builder_.store(Operand::constant(Type::Ptr, 0), offset(output, layout.data_offset));
    builder_.store(Operand::constant(Type::I32, 0), offset(output, layout.length_offset));
    builder_.store(Operand::constant(Type::I32, 0), offset(output, layout.capacity_offset));
    return {result, TemporaryValue{output}};
  }
  default:
    break;
  }
  auto receiver = dereference(arguments[0]);
  if (id == BuiltinId::Clone)
    return clone(std::move(receiver), result);
  if (id == BuiltinId::Push)
    return vector_push(std::move(receiver), arguments[1], result);
  if (id == BuiltinId::Remove)
    return vector_remove(std::move(receiver), arguments[1], result);
  Operand length = Operand::constant(Type::I32, 0);
  if (receiver.type->kind() == SemanticType::Kind::Array) {
    length = Operand::constant(Type::I32, static_cast<std::uint32_t>(receiver.type->length()));
  } else {
    const auto offset_bytes = module_.bridge.target().vector().length_offset;
    length = builder_.load(Type::I32, offset(address(receiver), offset_bytes));
  }
  if (id == BuiltinId::IsEmpty)
    length = builder_.compare(Predicate::Eq, length, Operand::constant(Type::I32, 0));
  return {result, ScalarValue{length}};
}

Value FunctionLowering::clone(Value source, const SemanticTypePtr &type) {
  const auto output = storage(type);
  const auto name = module_.helper(HelperKind::Clone, type);
  builder_.call(Type::Void, name, {output, address(std::move(source))});
  return temporary(type, output);
}

}
