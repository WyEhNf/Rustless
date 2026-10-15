#include "lowering.hpp"

namespace rx::ir {

Value FunctionLowering::emit(const ast::Expr &node, const ast::LoopExpr &payload) {
  const auto head = builder_.create_block("loop.head");
  const auto exit = builder_.create_block("loop.end");
  std::optional<Operand> output;
  const auto machine = module_.bridge.scalar_type(type(node));
  if (!machine || *machine != Type::Void)
    output = storage(type(node));
  builder_.branch(head);
  builder_.select(head);
  loops_.push_back({head, exit, type(node), output});
  block(*payload.body);
  if (!builder_.terminated())
    builder_.branch(head);
  const bool has_break = loops_.back().has_break;
  loops_.pop_back();
  builder_.select(exit);
  if (!has_break) {
    builder_.unreachable();
    return unit(type(node));
  }
  if (output)
    return read(type(node), *output);
  return unit(type(node));
}

Value FunctionLowering::emit(const ast::Expr &node, const ast::WhileExpr &payload) {
  const auto head = builder_.create_block("while.head");
  builder_.branch(head);
  builder_.select(head);
  auto condition = expression(*payload.condition);
  if (builder_.terminated())
    return unit(type(node));
  const auto body = builder_.create_block("while.body");
  const auto exit = builder_.create_block("while.end");
  builder_.cond_branch(scalar(condition), body, exit);
  builder_.select(body);
  loops_.push_back({head, exit, type(node), {}});
  block(*payload.body);
  if (!builder_.terminated())
    builder_.branch(head);
  loops_.pop_back();
  builder_.select(exit);
  return unit(type(node));
}

Value FunctionLowering::emit(const ast::Expr &node, const ast::BreakExpr &payload) {
  const auto loop = loops_.back();
  Value value = unit(loop.type);
  if (payload.value)
    value = expression(**payload.value);
  if (!builder_.terminated()) {
    if (loop.output)
      write(loop.type, *loop.output, convert(std::move(value), loop.type));
    loops_.back().has_break = true;
    builder_.branch(loop.exit);
  }
  return unit(type(node));
}

Value FunctionLowering::emit(const ast::Expr &node, const ast::ContinueExpr &) {
  builder_.branch(loops_.back().next);
  return unit(type(node));
}

Value FunctionLowering::emit(const ast::Expr &node, const ast::ReturnExpr &payload) {
  Value value = unit(result_type_);
  if (payload.value)
    value = expression(**payload.value);
  if (!builder_.terminated())
    return_value(std::move(value));
  return unit(type(node));
}

}
