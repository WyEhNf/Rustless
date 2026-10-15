#include "lowering.hpp"

namespace rx::ir {

Value FunctionLowering::branch(const ast::Node &node, const ast::IfExpr &payload) {
  auto condition = expression(*payload.condition);
  if (builder_.terminated())
    return unit(type(node));
  const auto result_type = type(node);
  const auto machine = module_.bridge.scalar_type(result_type);
  std::optional<Operand> output;
  if (!machine || *machine != Type::Void)
    output = storage(result_type);
  const auto consequent = builder_.create_block("if.then");
  const auto alternative = builder_.create_block("if.else");
  const auto join = builder_.create_block("if.end");
  builder_.cond_branch(scalar(condition), consequent, alternative);
  bool reaches_join = false;
  auto finish_branch = [&](Value value) {
    if (builder_.terminated())
      return;
    if (output)
      write(result_type, *output, convert(std::move(value), result_type));
    builder_.branch(join);
    reaches_join = true;
  };
  builder_.select(consequent);
  finish_branch(block(*payload.then_block));
  builder_.select(alternative);
  Value otherwise = unit(result_type);
  if (payload.else_branch) {
    auto lower_else = ast::Overloaded{
        [&](const ast::Box<ast::Block> &body) { return block(*body); },
        [&](const ast::Box<ast::IfBranch> &body) {
          auto value = branch(*body, body->data);
          if (!builder_.terminated())
            value = adjust(std::move(value), facts().expression(*body).adjustments);
          return value;
        },
    };
    otherwise = std::visit(lower_else, *payload.else_branch);
  }
  finish_branch(std::move(otherwise));
  builder_.select(join);
  if (!reaches_join) {
    builder_.unreachable();
    return unit(result_type);
  }
  if (output)
    return temporary(result_type, *output);
  return unit(result_type);
}

Value FunctionLowering::emit(const ast::Expr &node, const ast::IfExpr &payload) {
  return branch(node, payload);
}

Value FunctionLowering::short_circuit(const ast::Expr &node, const ast::BinaryExpr &payload,
                                    ShortCircuit operation) {
  auto left = expression(*payload.left);
  if (builder_.terminated())
    return unit(type(node));
  const auto predecessor = builder_.insertion_block();
  const auto right_block = builder_.create_block("logical.right");
  const auto join = builder_.create_block("logical.end");
  bool short_value = false;
  if (operation == ShortCircuit::And) {
    builder_.cond_branch(scalar(left), right_block, join);
  } else {
    short_value = true;
    builder_.cond_branch(scalar(left), join, right_block);
  }
  std::vector<PhiIncoming> incoming{
      {Operand::constant(Type::I1, short_value), predecessor}};
  builder_.select(right_block);
  auto right = expression(*payload.right);
  if (!builder_.terminated()) {
    incoming.push_back({scalar(right), builder_.insertion_block()});
    builder_.branch(join);
  }
  builder_.select(join);
  return {type(node), ScalarValue{builder_.phi(Type::I1, std::move(incoming))}};
}

}
