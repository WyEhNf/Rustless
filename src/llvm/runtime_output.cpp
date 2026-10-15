#include "lowering.hpp"

namespace rx::ir {

Function output_runtime() {
  Function function;
  function.name = "printInt";
  function.parameters.push_back({ValueId{1}, Type::I32, "value"});
  Builder builder(function);
  builder.select(builder.create_block("entry"));
  auto number = [](std::uint32_t bits) {
    return Operand::constant(Type::I32, bits);
  };
  const auto input = Operand::local(Type::I32, ValueId{1});
  const auto buffer = builder.allocate_entry(10, 1);
  const auto magnitude = builder.allocate_entry(4);
  const auto count = builder.allocate_entry(4);
  builder.store(number(0), count);
  const auto negative = builder.create_block("output.negative");
  const auto positive = builder.create_block("output.positive");
  const auto digits = builder.create_block("output.digits");
  const auto output = builder.create_block("output.head");
  const auto character = builder.create_block("output.character");
  const auto done = builder.create_block("output.done");
  builder.cond_branch(builder.compare(Predicate::SLt, input, number(0)), negative, positive);
  builder.select(negative);
  builder.call(Type::I32, "putchar", {number(45)});
  builder.store(builder.binary(BinaryOp::Sub, number(0), input), magnitude);
  builder.branch(digits);
  builder.select(positive);
  builder.store(input, magnitude);
  builder.branch(digits);
  builder.select(digits);
  const auto value = builder.load(Type::I32, magnitude);
  const auto index = builder.load(Type::I32, count);
  const auto digit = builder.binary(BinaryOp::URem, value, number(10));
  const auto ascii = builder.binary(BinaryOp::Add, digit, number(48));
  builder.store(builder.cast(CastOp::Trunc, Type::I8, ascii), builder.gep(buffer, index), 1);
  builder.store(builder.binary(BinaryOp::Add, index, number(1)), count);
  const auto remaining = builder.binary(BinaryOp::UDiv, value, number(10));
  builder.store(remaining, magnitude);
  builder.cond_branch(builder.compare(Predicate::Ne, remaining, number(0)), digits, output);
  builder.select(output);
  const auto size = builder.load(Type::I32, count);
  builder.cond_branch(builder.compare(Predicate::Ne, size, number(0)), character, done);
  builder.select(character);
  const auto previous = builder.binary(BinaryOp::Sub, size, number(1));
  builder.store(previous, count);
  const auto byte = builder.load(Type::I8, builder.gep(buffer, previous), 1);
  builder.call(Type::I32, "putchar", {builder.cast(CastOp::ZExt, Type::I32, byte)});
  builder.branch(output);
  builder.select(done);
  builder.ret();
  return function;
}

}
