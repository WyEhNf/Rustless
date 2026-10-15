#include "lowering.hpp"

namespace rx::ir {

Operand FunctionLowering::storage(const SemanticTypePtr &type) {
  const auto layout = module_.bridge.layout(type);
  return builder_.allocate_entry(layout.bytes, layout.alignment);
}

Operand FunctionLowering::offset(Operand base, std::uint32_t bytes) {
  if (bytes == 0)
    return base;
  return offset(std::move(base), Operand::constant(Type::I32, bytes));
}

Operand FunctionLowering::offset(Operand base, Operand bytes) {
  return builder_.gep(std::move(base), std::move(bytes));
}

Value FunctionLowering::read(const SemanticTypePtr &type, Operand source) {
  const auto machine = module_.bridge.scalar_type(type);
  if (!machine)
    return {type, StoredValue{std::move(source)}};
  if (*machine == Type::Void)
    return unit(type);
  const auto layout = module_.bridge.layout(type);
  return {type, ScalarValue{builder_.load(*machine, std::move(source), layout.alignment)}};
}

Value FunctionLowering::temporary(const SemanticTypePtr &type, Operand source) {
  if (!module_.bridge.scalar_type(type))
    return {type, TemporaryValue{std::move(source)}};
  return read(type, std::move(source));
}

Operand FunctionLowering::scalar(Value value) {
  if (const auto *stored = std::get_if<StoredValue>(&value.data))
    value = read(value.type, stored->address);
  return std::get<ScalarValue>(value.data).operand;
}

Operand FunctionLowering::address(Value value) {
  if (const auto *stored = std::get_if<StoredValue>(&value.data))
    return stored->address;
  if (const auto *temporary = std::get_if<TemporaryValue>(&value.data))
    return temporary->address;
  const auto type = value.type;
  auto result = storage(type);
  write(type, result, std::move(value));
  return result;
}

void FunctionLowering::copy(Operand destination, Operand source, std::uint32_t bytes) {
  if (bytes != 0)
    builder_.call(Type::Ptr, "memmove",
                  {destination, source, Operand::constant(Type::I32, bytes)});
}

void FunctionLowering::write(const SemanticTypePtr &type, Operand destination, Value source) {
  const auto machine = module_.bridge.scalar_type(type);
  const auto layout = module_.bridge.layout(type);
  if (!machine) {
    copy(std::move(destination), address(std::move(source)), layout.bytes);
  } else if (*machine != Type::Void) {
    builder_.store(scalar(std::move(source)), std::move(destination), layout.alignment);
  }
}

Value FunctionLowering::freeze(Value value) {
  if (!std::holds_alternative<StoredValue>(value.data))
    return value;
  if (module_.bridge.scalar_type(value.type))
    return read(value.type, address(value));
  const auto temporary = storage(value.type);
  write(value.type, temporary, value);
  return {value.type, TemporaryValue{temporary}};
}

void FunctionLowering::bind(SymbolId symbol, const SemanticTypePtr &type, Value value) {
  if (const auto *temporary = std::get_if<TemporaryValue>(&value.data)) {
    bindings_.emplace(symbol, Value{type, StoredValue{temporary->address}});
    return;
  }
  const auto destination = storage(type);
  write(type, destination, convert(std::move(value), type));
  bindings_.emplace(symbol, Value{type, StoredValue{destination}});
}

}
