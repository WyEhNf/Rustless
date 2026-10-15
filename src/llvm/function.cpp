#include "lowering.hpp"

namespace rx::ir {

FunctionLowering::FunctionLowering(ModuleLowering &module, std::string name, Type result)
    : module_(module), builder_(function_) {
  function_.name = std::move(name);
  function_.result = result;
  builder_.select(builder_.create_block("entry"));
}

Operand FunctionLowering::parameter(Type type, std::string name,
                                  std::optional<SymbolId> symbol) {
  const auto id = function_.next_value;
  ++function_.next_value.value;
  function_.parameters.push_back({id, type, std::move(name), symbol});
  return Operand::local(type, id);
}

void FunctionLowering::body(const ast::Function &node) {
  const auto signature = module_.bridge.function(node);
  result_type_ = facts().function(signature.symbol).result;
  function_.symbol = signature.symbol;
  function_.origin = ir::origin(node);
  if (signature.result.passing == Passing::Indirect)
    output_ = parameter(Type::Ptr, "return");
  auto bind_parameter = [&](const AbiParameter &input) {
    const auto &semantic_type = facts().symbol_type(input.symbol);
    if (input.value.type == Type::Void) {
      bind(input.symbol, semantic_type, unit(semantic_type));
      return;
    }
    auto argument = parameter(input.value.type, facts().symbol(input.symbol).name, input.symbol);
    Value value{semantic_type, ScalarValue{argument}};
    if (input.value.passing == Passing::Indirect)
      value.data = StoredValue{argument};
    bind(input.symbol, semantic_type, value);
  };
  if (signature.receiver)
    bind_parameter(*signature.receiver);
  for (const auto &input : signature.parameters)
    bind_parameter(input);
  auto result = block(*node.body);
  if (!builder_.terminated())
    return_value(std::move(result));
}

void FunctionLowering::return_value(Value value) {
  value = convert(std::move(value), result_type_);
  if (output_) {
    write(result_type_, *output_, std::move(value));
    builder_.ret();
  } else if (function_.result == Type::Void) {
    builder_.ret();
  } else {
    builder_.ret(scalar(std::move(value)));
  }
}

}
