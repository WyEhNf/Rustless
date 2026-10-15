#include "lowering.hpp"

namespace rx::ir {

Function input_runtime();
Function output_runtime();

void add_runtime(Module &module) {
  module.declarations = {
      {"getchar", Type::I32, {}},
      {"putchar", Type::I32, {Type::I32}},
      {"malloc", Type::Ptr, {Type::I32}},
      {"memmove", Type::Ptr, {Type::Ptr, Type::Ptr, Type::I32}},
  };
  module.functions.push_back(input_runtime());
  module.functions.push_back(output_runtime());
  Function newline;
  newline.name = "printlnInt";
  newline.parameters.push_back({ValueId{1}, Type::I32, "value"});
  Builder line(newline);
  line.select(line.create_block("entry"));
  line.call(Type::Void, "printInt", {Operand::local(Type::I32, ValueId{1})});
  line.call(Type::I32, "putchar", {Operand::constant(Type::I32, 10)});
  line.ret();
  module.functions.push_back(std::move(newline));
  Function allocation;
  allocation.name = "__rx_alloc";
  allocation.result = Type::Ptr;
  allocation.parameters = {{ValueId{1}, Type::I32, "bytes"}, {ValueId{2}, Type::I32, "alignment"}};
  Builder allocator(allocation);
  allocator.select(allocator.create_block("entry"));
  const auto bytes = allocator.binary(
      BinaryOp::Add, Operand::local(Type::I32, ValueId{1}), Operand::constant(Type::I32, 1));
  allocator.ret(allocator.call(Type::Ptr, "malloc", {bytes}));
  module.functions.push_back(std::move(allocation));
}

}
