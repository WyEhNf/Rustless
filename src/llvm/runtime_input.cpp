#include "lowering.hpp"

namespace rx::ir {

Function input_runtime() {
  Function function;
  function.name = "getInt";
  function.result = Type::I32;
  Builder builder(function);
  builder.select(builder.create_block("entry"));
  auto number = [](std::uint32_t bits) {
    return Operand::constant(Type::I32, bits);
  };
  const auto character = builder.allocate_entry(4);
  const auto value = builder.allocate_entry(4);
  builder.store(builder.call(Type::I32, "getchar", {}).value(), character);
  builder.store(number(0), value);
  const auto skip = builder.create_block("input.whitespace");
  const auto advance = builder.create_block("input.advance");
  const auto sign = builder.create_block("input.sign");
  const auto signed_digit = builder.create_block("input.signed_digit");
  const auto digits = builder.create_block("input.digits");
  const auto digit = builder.create_block("input.digit");
  const auto done = builder.create_block("input.done");
  const auto negative_return = builder.create_block("input.negative");
  const auto positive_return = builder.create_block("input.positive");
  builder.branch(skip);
  builder.select(skip);
  const auto next = builder.load(Type::I32, character);
  const auto whitespace = builder.compare(Predicate::SLt, next, number(33));
  const auto present = builder.compare(Predicate::Ne, next, number(0xffffffffu));
  builder.cond_branch(builder.binary(BinaryOp::And, whitespace, present), advance, sign);
  builder.select(advance);
  builder.store(builder.call(Type::I32, "getchar", {}).value(), character);
  builder.branch(skip);
  builder.select(sign);
  const auto initial = builder.load(Type::I32, character);
  const auto negative = builder.compare(Predicate::Eq, initial, number(45));
  const auto positive = builder.compare(Predicate::Eq, initial, number(43));
  builder.cond_branch(builder.binary(BinaryOp::Or, negative, positive), signed_digit, digits);
  builder.select(signed_digit);
  builder.store(builder.call(Type::I32, "getchar", {}).value(), character);
  builder.branch(digits);
  builder.select(digits);
  const auto current = builder.load(Type::I32, character);
  const auto lower = builder.compare(Predicate::UGe, current, number(48));
  const auto upper = builder.compare(Predicate::ULe, current, number(57));
  builder.cond_branch(builder.binary(BinaryOp::And, lower, upper), digit, done);
  builder.select(digit);
  const auto scaled = builder.binary(BinaryOp::Mul, builder.load(Type::I32, value), number(10));
  const auto unit = builder.binary(BinaryOp::Sub, current, number(48));
  builder.store(builder.binary(BinaryOp::Add, scaled, unit), value);
  builder.store(builder.call(Type::I32, "getchar", {}).value(), character);
  builder.branch(digits);
  builder.select(done);
  const auto result = builder.load(Type::I32, value);
  builder.cond_branch(negative, negative_return, positive_return);
  builder.select(negative_return);
  builder.ret(builder.binary(BinaryOp::Sub, number(0), result));
  builder.select(positive_return);
  builder.ret(result);
  return function;
}

}
