#include "lowering.hpp"

namespace rx::ir {

Value FunctionLowering::emit(const ast::Expr &node, const ast::CallExpr &payload) {
  const auto call = module_.bridge.call(node, payload);
  expression(*payload.callee);
  if (builder_.terminated())
    return unit(type(node));
  std::vector<Value> arguments;
  for (const auto &argument : payload.arguments) {
    auto value = expression(*argument);
    if (builder_.terminated())
      return unit(type(node));
    arguments.push_back(std::move(value));
  }
  return invoke(call, std::move(arguments));
}

Value FunctionLowering::emit(const ast::Expr &node, const ast::MethodCallExpr &payload) {
  const auto call = module_.bridge.call(node, payload);
  auto receiver = intrinsic(*payload.receiver);
  if (builder_.terminated())
    return unit(type(node));
  receiver = adjust(std::move(receiver), call.receiver->adjustments);
  std::vector<Value> arguments;
  arguments.push_back(freeze(std::move(receiver)));
  for (const auto &argument : payload.arguments) {
    auto value = expression(*argument);
    if (builder_.terminated())
      return unit(type(node));
    arguments.push_back(std::move(value));
  }
  return invoke(call, std::move(arguments));
}

Value FunctionLowering::invoke(const CallLowering &call, std::vector<Value> arguments) {
  if (const auto *builtin_call = std::get_if<BuiltinCall>(&call.target))
    return builtin(builtin_call->builtin, std::move(arguments), call.result);
  const auto &signature = std::get<FunctionCall>(call.target).signature;
  std::vector<Operand> operands;
  std::optional<Operand> output;
  Type result_type = signature.result.type;
  if (signature.result.passing == Passing::Indirect) {
    output = storage(call.result);
    operands.push_back(*output);
    result_type = Type::Void;
  }
  std::vector<AbiParameter> parameters;
  if (signature.receiver)
    parameters.push_back(*signature.receiver);
  parameters.insert(parameters.end(), signature.parameters.begin(), signature.parameters.end());
  for (std::size_t index = 0; index < arguments.size(); ++index) {
    const auto &parameter = parameters[index];
    auto value = convert(std::move(arguments[index]), facts().symbol_type(parameter.symbol));
    if (parameter.value.type == Type::Void)
      continue;
    if (parameter.value.passing == Passing::Indirect)
      operands.push_back(address(std::move(value)));
    else
      operands.push_back(scalar(std::move(value)));
  }
  const auto name = module_.function_name(signature.symbol);
  auto returned = builder_.call(result_type, name, std::move(operands));
  if (output)
    return {call.result, StoredValue{*output}};
  if (returned)
    return {call.result, ScalarValue{*returned}};
  return unit(call.result);
}

}
