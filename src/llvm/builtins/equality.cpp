#include "llvm/lowering.hpp"

namespace rx::ir {

void FunctionLowering::equality_body(const SemanticTypePtr &type, Operand left, Operand right) {
  const auto unequal = builder_.create_block("equal.false");
  auto require = [&](Operand condition) {
    const auto next = builder_.create_block("equal.next");
    builder_.cond_branch(condition, next, unequal);
    builder_.select(next);
  };
  auto equal_child = [&](const SemanticTypePtr &child, Operand lhs, Operand rhs) {
    const auto name = module_.helper(HelperKind::Equal, child);
    require(builder_.call(Type::I1, name, {lhs, rhs}).value());
  };
  switch (type->kind()) {
  case SemanticType::Kind::Struct:
    for (const auto &field : module_.bridge.structure(type->symbol()).fields)
      equal_child(facts().symbol_type(field.symbol), offset(left, field.offset),
          offset(right, field.offset));
    break;
  case SemanticType::Kind::Array:
  case SemanticType::Kind::Vec: {
    Operand count = Operand::constant(Type::I32, 0);
    if (type->kind() == SemanticType::Kind::Vec) {
      const auto vector = module_.bridge.target().vector();
      count = builder_.load(Type::I32, offset(left, vector.length_offset));
      const auto right_length = builder_.load(Type::I32, offset(right, vector.length_offset));
      require(builder_.compare(Predicate::Eq, count, right_length));
      left = builder_.load(Type::Ptr, left);
      right = builder_.load(Type::Ptr, right);
    } else {
      count = Operand::constant(Type::I32, static_cast<std::uint32_t>(type->length()));
    }
    const auto stride = Operand::constant(Type::I32, module_.bridge.layout(type->element()).bytes);
    each(count, [&](Operand index) {
      const auto bytes = builder_.binary(BinaryOp::Mul, index, stride);
      equal_child(type->element(), offset(left, bytes), offset(right, bytes));
    });
    break;
  }
  case SemanticType::Kind::Ref:
  case SemanticType::Kind::Box:
    equal_child(type->element(), builder_.load(Type::Ptr, left), builder_.load(Type::Ptr, right));
    break;
  case SemanticType::Kind::Unit:
    break;
  default:
    require(builder_.compare(Predicate::Eq, scalar(read(type, left)), scalar(read(type, right))));
    break;
  }
  builder_.ret(Operand::constant(Type::I1, 1));
  builder_.select(unequal);
  builder_.ret(Operand::constant(Type::I1, 0));
}

}
