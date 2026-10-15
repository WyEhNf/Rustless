#include "lowering.hpp"

namespace rx::ir {

std::string ModuleLowering::function_name(SymbolId symbol) const {
  return "rx_fn_" + std::to_string(symbol.value);
}

std::string ModuleLowering::helper(HelperKind kind, const SemanticTypePtr &type) {
  const auto key = std::make_pair(kind, type_name(type));
  auto found = helper_names_.find(key);
  if (found != helper_names_.end())
    return found->second;
  std::string name = "__rx_clone_";
  if (kind == HelperKind::Equal)
    name = "__rx_equal_";
  name += std::to_string(helpers_.size());
  helper_names_.emplace(key, name);
  helpers_.push_back({kind, type, name});
  return name;
}

Module ModuleLowering::run() {
  auto lower_function = [&](const ast::Function &node, const ast::Function &) {
    const auto signature = bridge.function(node);
    Type result = signature.result.type;
    if (signature.result.passing == Passing::Indirect)
      result = Type::Void;
    FunctionLowering lowering(*this, function_name(signature.symbol), result);
    lowering.body(node);
    module.functions.push_back(lowering.take());
  };
  auto lower_member = ast::Overloaded{
      lower_function,
      [](const ast::Constant &, const ast::Constant &) {},
  };
  auto lower_item = ast::Overloaded{
      lower_function,
      [&](const ast::Impl &node, const ast::Impl &) {
        for (const auto &member : node.members)
          ast::visit(member, lower_member);
      },
      [](const ast::Use &, const ast::Use &) {},
      [](const ast::Struct &, const ast::Struct &) {},
      [](const ast::Constant &, const ast::Constant &) {},
  };
  for (const auto &item : bridge.facts().crate().items)
    ast::visit(item, lower_item);
  for (std::size_t index = 0; index < helpers_.size(); ++index) {
    const auto request = helpers_[index];
    Type result = Type::Void;
    if (request.kind == HelperKind::Equal)
      result = Type::I1;
    FunctionLowering lowering(*this, request.name, result);
    lowering.helper_body(request);
    module.functions.push_back(lowering.take());
  }
  for (const auto &item : bridge.facts().crate().items) {
    const auto *function = std::get_if<ast::Function>(&item);
    if (!function || function->name.text != "main")
      continue;
    Function entry;
    entry.name = "main";
    entry.result = Type::I32;
    Builder builder(entry);
    builder.select(builder.create_block("entry"));
    const auto symbol = bridge.facts().declaration(*function).id;
    builder.call(Type::Void, function_name(symbol), {});
    builder.ret(Operand::constant(Type::I32, 0));
    module.functions.push_back(std::move(entry));
  }
  add_runtime(module);
  return std::move(module);
}

Module lower(const ast::Crate &, const SemanticResult &semantic) {
  ModuleLowering lowering(semantic.data());
  return lowering.run();
}

}
