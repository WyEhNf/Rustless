#include "rx/semantic_index.hpp"
#include "rx/ast/visitor.hpp"
#include <functional>
#include <stdexcept>
#include <unordered_set>

namespace rx {
namespace {
template <class Map, class Id, class Fact>
void insert(Map &map, Id id, const Fact &fact) {
  if (!id.value || !map.emplace(id, &fact).second)
    throw std::invalid_argument("duplicate or zero semantic identity");
}
}
SemanticIndex::SemanticIndex(const ast::Crate &crate,
                             const SemanticResult &result)
    : SemanticIndex(crate, result.snapshot(), ValidationTag{}) {}
SemanticIndex::SemanticIndex(const ast::Crate &crate,
                             std::shared_ptr<const SemanticData> snapshot,
                             ValidationTag)
    : crate_(&crate), snapshot_(std::move(snapshot)) {
  if (!snapshot_ || !snapshot_->success || snapshot_->source_crate != &crate)
    throw std::invalid_argument(
        "semantic result is not checked for this crate");
  const auto &data = *snapshot_;
  for (const auto &fact : data.symbols) {
    insert(symbols_, fact.id, fact);
    insert(declarations_, fact.declaration, fact);
  }
  for (const auto &fact : data.expressions)
    insert(expressions_, fact.node, fact);
  for (const auto &fact : data.resolved_types)
    insert(types_, fact.node, fact);
  for (const auto &fact : data.symbol_types)
    insert(symbol_types_, fact.symbol, fact);
  for (const auto &fact : data.functions)
    insert(functions_, fact.symbol, fact);
  for (const auto &fact : data.structures)
    insert(structures_, fact.symbol, fact);
  for (const auto &fact : data.constants)
    insert(constants_, fact.symbol, fact);
  validate_links();
  validate_tree();
  validate_declarations();
  validate_shapes();
}
SemanticResult::SemanticResult()
    : data_(std::make_shared<const SemanticData>()) {}
SemanticResult SemanticResult::publish(const ast::Crate &crate,
                                       SemanticData data) {
  data.source_crate = &crate;
  data.success = true;
  auto snapshot = std::make_shared<const SemanticData>(std::move(data));
  SemanticIndex validated(crate, snapshot, SemanticIndex::ValidationTag{});
  return SemanticResult(std::move(snapshot));
}
SemanticResult SemanticResult::rejected(const ast::Crate &crate,
                                        SemanticData data) {
  data.source_crate = &crate;
  data.success = false;
  return SemanticResult(std::make_shared<const SemanticData>(std::move(data)));
}
const SymbolRecord &SemanticIndex::symbol(SymbolId id) const {
  return *symbols_.at(id);
}
const SymbolRecord &SemanticIndex::declaration(ast::NodeId id) const {
  return *declarations_.at(id);
}
const ExpressionSemantics &SemanticIndex::expression(ast::NodeId id) const {
  return *expressions_.at(id);
}
const SemanticTypePtr &SemanticIndex::type(ast::NodeId id) const {
  return types_.at(id)->type;
}
const SemanticTypePtr &SemanticIndex::symbol_type(SymbolId id) const {
  return symbol_types_.at(id)->type;
}
const FunctionSemantics &SemanticIndex::function(SymbolId id) const {
  return *functions_.at(id);
}
const StructSemantics &SemanticIndex::structure(SymbolId id) const {
  return *structures_.at(id);
}
const ConstantValue &SemanticIndex::constant(SymbolId id) const {
  return *constants_.at(id);
}
void SemanticIndex::require_node(const ast::Node &node) const {
  const auto found = nodes_.find(node.id);
  if (found == nodes_.end() || found->second != &node)
    throw std::invalid_argument("semantic query uses a foreign AST node");
}
void SemanticIndex::validate_links() const {
  std::unordered_set<const SemanticType *> checked_types;
  std::unordered_set<const SemanticType *> active_types;
  std::function<void(const SemanticTypePtr &)> require_type;
  require_type = [&](const SemanticTypePtr &type) {
    if (!type || type->kind() == SemanticType::Kind::Error)
      throw std::invalid_argument("checked semantic fact has no valid type");
    if (checked_types.find(type.get()) != checked_types.end())
      return;
    if (!active_types.insert(type.get()).second)
      throw std::invalid_argument("cyclic semantic type object graph");
    rx::visit(*type, [&](const SemanticType &, const auto &payload) {
      using Payload = std::decay_t<decltype(payload)>;
      if constexpr (std::is_same_v<Payload, BoxType> ||
                    std::is_same_v<Payload, VecType> ||
                    std::is_same_v<Payload, ArrayType> ||
                    std::is_same_v<Payload, RefType>)
        require_type(payload.element);
      if constexpr (std::is_same_v<Payload, BuiltinFunctionType>)
        if (payload.owner)
          require_type(payload.owner);
      if constexpr (std::is_same_v<Payload, StructType> ||
                    std::is_same_v<Payload, FunctionType>) {
        const auto &record = symbol(payload.symbol);
        bool correct_kind;
        if constexpr (std::is_same_v<Payload, StructType>) {
          correct_kind = record.kind == SymbolKind::Struct;
        } else {
          correct_kind = record.kind == SymbolKind::Function ||
                         record.kind == SymbolKind::AssociatedFunction;
        }
        if (!correct_kind || record.name != payload.name)
          throw std::invalid_argument(
              "semantic nominal type identity mismatch");
      }
    });
    active_types.erase(type.get());
    checked_types.insert(type.get());
  };
  for (const auto &[_, fact] : symbols_) {
    if (fact->owner)
      (void)symbol(*fact->owner);
    switch (fact->kind) {
    case SymbolKind::Struct:
      (void)structure(fact->id);
      break;
    case SymbolKind::Function:
    case SymbolKind::AssociatedFunction:
      (void)function(fact->id);
      break;
    case SymbolKind::Constant:
    case SymbolKind::AssociatedConstant:
      (void)constant(fact->id);
      require_type(symbol_type(fact->id));
      break;
    default:
      require_type(symbol_type(fact->id));
      break;
    }
  }
  for (const auto &[_, fact] : expressions_) {
    require_type(fact->type);
    if (fact->symbol)
      (void)symbol(*fact->symbol);
    for (const auto &adjustment : fact->adjustments)
      require_type(adjustment.target);
    if (fact->callable)
      std::visit(ast::Overloaded{[&](const FunctionTarget &target) {
                                   (void)function(target.symbol);
                                 },
                                 [&](const BuiltinTarget &target) {
                                   (void)builtin_name(target.builtin);
                                 }},
                 *fact->callable);
    for (const auto *operation : {&fact->binary, &fact->compound})
      if (*operation) {
        require_type((*operation)->result);
        for (const auto *operand :
             {&(*operation)->left, &(*operation)->right}) {
          (void)expression(operand->node);
          require_type(operand->effective_type);
          for (const auto &adjustment : operand->adjustments)
            require_type(adjustment.target);
        }
      }
    if (fact->unary) {
      require_type(fact->unary->result);
      require_type(fact->unary->operand.effective_type);
      (void)expression(fact->unary->operand.node);
      for (const auto &adjustment : fact->unary->operand.adjustments)
        require_type(adjustment.target);
    }
    if (fact->receiver) {
      require_type(fact->receiver->effective_type);
      (void)expression(fact->receiver->node);
      for (const auto &adjustment : fact->receiver->adjustments)
        require_type(adjustment.target);
    }
  }
  for (const auto &[_, fact] : types_)
    require_type(fact->type);
  for (const auto &[id, fact] : symbol_types_) {
    (void)symbol(id);
    require_type(fact->type);
  }
  for (const auto &[id, fact] : functions_) {
    auto kind = symbol(id).kind;
    if (kind != SymbolKind::Function && kind != SymbolKind::AssociatedFunction)
      throw std::invalid_argument(
          "function fact links to a non-function symbol");
    require_type(fact->result);
    auto parameter = [&](const ParameterSemantics &value, SymbolKind expected) {
      const auto &record = symbol(value.symbol);
      if (record.kind != expected || record.owner != id)
        throw std::invalid_argument("parameter owner or kind mismatch");
      require_type(value.type);
    };
    if (fact->receiver)
      parameter(*fact->receiver, SymbolKind::Receiver);
    for (const auto &value : fact->parameters)
      parameter(value, SymbolKind::Parameter);
  }
  for (const auto &[id, fact] : structures_) {
    if (symbol(id).kind != SymbolKind::Struct)
      throw std::invalid_argument("struct fact links to a non-struct symbol");
    std::unordered_set<SymbolId> seen;
    for (auto field : fact->fields) {
      const auto &record = symbol(field);
      if (record.kind != SymbolKind::Field || record.owner != id ||
          !seen.insert(field).second)
        throw std::invalid_argument("field owner, kind or identity mismatch");
      require_type(symbol_type(field));
    }
  }
  for (const auto &[id, _] : constants_) {
    auto kind = symbol(id).kind;
    if (kind != SymbolKind::Constant && kind != SymbolKind::AssociatedConstant)
      throw std::invalid_argument(
          "constant fact links to a non-constant symbol");
  }
}
void SemanticIndex::validate_tree() {
  std::unordered_set<ast::NodeId> identities;
  std::unordered_set<ast::NodeId> expression_nodes;
  std::unordered_set<ast::NodeId> type_nodes;

  ast::walk(*crate_, [&](const ast::Node &node, const auto &) {
    if (!node.id.value || !identities.insert(node.id).second ||
        node.span.file != crate_->source.id ||
        node.span.begin > node.span.end ||
        node.span.end > crate_->source.text.size())
      throw std::invalid_argument("invalid AST identity or source span");
    nodes_.emplace(node.id, &node);
  });
  auto check_ids = [&](const auto &facts) {
    for (const auto &[id, _] : facts)
      if (identities.find(id) == identities.end())
        throw std::invalid_argument("semantic fact refers outside its AST");
  };
  check_ids(declarations_);
  check_ids(expressions_);
  check_ids(types_);
  ast::walk(*crate_, [&](const auto &owner, const auto &) {
    using Payload = std::decay_t<decltype(owner)>;

    if constexpr (std::is_same_v<Payload, ast::TypePredicate>)
      return ast::WalkControl::SkipChildren;
    if constexpr (std::is_same_v<Payload, ast::Expr> ||
                  std::is_same_v<Payload, ast::Block> ||
                  std::is_same_v<Payload, ast::IfBranch>) {
      (void)expression(owner.id);
      expression_nodes.insert(owner.id);
    }
    if constexpr (std::is_same_v<Payload, ast::TypeSyntax>) {
      (void)type(owner.id);
      type_nodes.insert(owner.id);
    }
    return ast::WalkControl::Continue;
  });
  if (expression_nodes.size() != expressions_.size() ||
      type_nodes.size() != types_.size())
    throw std::invalid_argument("semantic fact is attached to wrong AST shape");
}
namespace {
void require_equivalent(const SemanticTypePtr &left,
                        const SemanticTypePtr &right) {
  if (!same_type(left, right))
    throw std::invalid_argument("semantic declaration type mismatch");
}
ast::BinaryOp source_operator(const ResolvedOperation &operation) {
  using BinaryOp = ast::BinaryOp;
  return std::visit(
      ast::Overloaded{
          [](const IntegerArithmetic &payload) {
            constexpr std::array values{BinaryOp::Add, BinaryOp::Sub,
                                        BinaryOp::Mul, BinaryOp::Div,
                                        BinaryOp::Rem};
            return values.at(static_cast<std::size_t>(payload.op));
          },
          [](const BitwiseOperation &payload) {
            constexpr std::array values{BinaryOp::BitAnd, BinaryOp::BitOr,
                                        BinaryOp::BitXor};
            return values.at(static_cast<std::size_t>(payload.op));
          },
          [](const ShiftOperation &payload) {
            if (payload.count_policy != ShiftCountPolicy::MaskToWidth)
              throw std::invalid_argument("invalid shift count policy");
            constexpr std::array values{BinaryOp::Shl, BinaryOp::Shr};
            return values.at(static_cast<std::size_t>(payload.direction));
          },
          [](const ScalarComparison &payload) {
            constexpr std::array values{BinaryOp::Eq, BinaryOp::Ne,
                                        BinaryOp::Lt, BinaryOp::Le,
                                        BinaryOp::Gt, BinaryOp::Ge};
            return values.at(static_cast<std::size_t>(payload.relation));
          },
          [](const StructuralEquality &payload) {
            constexpr std::array values{BinaryOp::Eq, BinaryOp::Ne};
            return values.at(static_cast<std::size_t>(payload.op));
          },
          [](const LogicalOperation &payload) {
            constexpr std::array values{BinaryOp::LogicalAnd,
                                        BinaryOp::LogicalOr};
            return values.at(static_cast<std::size_t>(payload.op));
          }},
      operation);
}
void validate_operand(const SemanticIndex &index,
                      const OperandSemantics &operand) {
  using TypeKind = SemanticType::Kind;
  auto current = index.expression(operand.node).type;
  for (const auto &adjustment : operand.adjustments) {
    const auto &target = adjustment.target;
    switch (adjustment.kind) {
    case Adjustment::Kind::Dereference:
      if (current->kind() != TypeKind::Ref && current->kind() != TypeKind::Box)
        throw std::invalid_argument("dereference adjustment has no referent");
      require_equivalent(current->element(), target);
      break;
    case Adjustment::Kind::BorrowShared:
    case Adjustment::Kind::BorrowMutable:
      if (target->kind() != TypeKind::Ref ||
          target->is_mutable() !=
              (adjustment.kind == Adjustment::Kind::BorrowMutable))
        throw std::invalid_argument(
            "borrow adjustment has wrong reference type");
      require_equivalent(current, target->element());
      break;
    case Adjustment::Kind::MutToShared:
      if (current->kind() != TypeKind::Ref || !current->is_mutable() ||
          target->kind() != TypeKind::Ref || target->is_mutable())
        throw std::invalid_argument("mutable-to-shared adjustment mismatch");
      require_equivalent(current->element(), target->element());
      break;
    case Adjustment::Kind::NeverToAny:
      if (current->kind() != TypeKind::Never)
        throw std::invalid_argument("never coercion has non-never source");
      break;
    default:
      throw std::invalid_argument("unknown operand adjustment");
    }
    current = target;
  }
  require_equivalent(current, operand.effective_type);
}
void validate_operation(const SemanticIndex &index, const ResolvedBinary &fact,
                        const ast::Expr &left, const ast::Expr &right,
                        ast::BinaryOp op) {
  if (fact.left.node != left.id || fact.right.node != right.id ||
      source_operator(fact.operation) != op)
    throw std::invalid_argument(
        "resolved operation does not match source operands");
  validate_operand(index, fact.left);
  validate_operand(index, fact.right);
  const auto expected = resolve_operation(op, fact.left.effective_type,
                                          fact.right.effective_type);
  if (!expected || expected->index() != fact.operation.index())
    throw std::invalid_argument(
        "resolved operation category is invalid for types");
  const bool boolean =
      std::holds_alternative<ScalarComparison>(fact.operation) ||
      std::holds_alternative<StructuralEquality>(fact.operation) ||
      std::holds_alternative<LogicalOperation>(fact.operation);
  if (boolean) {
    if (fact.result->kind() != SemanticType::Kind::Bool)
      throw std::invalid_argument("comparison or logical result is not bool");
  } else {
    require_equivalent(fact.result, fact.left.effective_type);
  }
}
bool same_target(const CallTarget &left, const CallTarget &right) {
  return std::visit(
      ast::Overloaded{[&](const FunctionTarget &target) {
                        const auto *other = std::get_if<FunctionTarget>(&right);
                        return other && target.symbol == other->symbol;
                      },
                      [&](const BuiltinTarget &target) {
                        const auto *other = std::get_if<BuiltinTarget>(&right);
                        return other && target.builtin == other->builtin;
                      }},
      left);
}
}
void SemanticIndex::validate_declarations() const {
  std::unordered_set<ast::NodeId> expected;
  auto require_symbol = [&](const ast::Node &node, SymbolKind kind,
                            std::optional<SymbolId> owner = {},
                            std::string_view name = {}) -> SymbolId {
    const auto &record = declaration(node.id);
    if (record.kind != kind || record.owner != owner ||
        (!name.empty() && record.name != name))
      throw std::invalid_argument("AST declaration kind or owner mismatch");
    expected.insert(node.id);
    return record.id;
  };
  auto check_struct = [&](const ast::Struct &node) {
    const auto id =
        require_symbol(node, SymbolKind::Struct, {}, node.name.text);
    const auto &fact = structure(id);
    if (fact.fields.size() != node.fields.size())
      throw std::invalid_argument("semantic struct field count mismatch");
    for (std::size_t index = 0; index < node.fields.size(); ++index) {
      const auto &field = node.fields[index];
      const auto field_id =
          require_symbol(field, SymbolKind::Field, id, field.name.text);
      if (fact.fields[index] != field_id)
        throw std::invalid_argument("semantic struct field order mismatch");
      require_equivalent(symbol_type(field_id), type(*field.type));
    }
  };
  auto check_function = [&](const ast::Function &node,
                            std::optional<SymbolId> owner = {}) {
    auto kind = SymbolKind::Function;
    auto name = node.name.text;
    if (owner) {
      kind = SymbolKind::AssociatedFunction;
      name = symbol(*owner).name + "::" + name;
    }
    const auto id = require_symbol(node, kind, owner, name);
    const auto &fact = function(id);
    if (fact.parameters.size() != node.signature.parameters.size() ||
        fact.receiver.has_value() != node.signature.receiver.has_value())
      throw std::invalid_argument("semantic function signature count mismatch");
    for (std::size_t index = 0; index < node.signature.parameters.size();
         ++index) {
      const auto &parameter = node.signature.parameters[index];
      const auto parameter_id = require_symbol(parameter, SymbolKind::Parameter,
                                               id, parameter.binding.name.text);
      if (fact.parameters[index].symbol != parameter_id)
        throw std::invalid_argument("semantic parameter order mismatch");
      require_equivalent(fact.parameters[index].type, type(*parameter.type));
      require_equivalent(fact.parameters[index].type,
                         symbol_type(parameter_id));
    }
    if (fact.receiver) {
      const auto &receiver = *node.signature.receiver;
      const auto receiver_id =
          require_symbol(receiver, SymbolKind::Receiver, id, "self");
      if (!owner || fact.receiver->symbol != receiver_id)
        throw std::invalid_argument("semantic receiver identity mismatch");
      require_equivalent(fact.receiver->type, symbol_type(receiver_id));
      auto receiver_type = fact.receiver->type;
      if (ast::by_reference(receiver)) {
        if (receiver_type->kind() != SemanticType::Kind::Ref ||
            receiver_type->is_mutable() != ast::is_mutable(receiver))
          throw std::invalid_argument("semantic receiver reference mismatch");
        receiver_type = receiver_type->element();
      }
      if (receiver_type->kind() != SemanticType::Kind::Struct ||
          receiver_type->symbol() != *owner)
        throw std::invalid_argument("semantic receiver owner type mismatch");
    }
    if (node.signature.return_annotation)
      require_equivalent(fact.result, type(**node.signature.return_annotation));
    else if (fact.result->kind() != SemanticType::Kind::Unit)
      throw std::invalid_argument("unannotated function result is not unit");
    ast::walk(*node.body, [&](const auto &statement, const auto &payload) {
      if constexpr (std::is_same_v<std::decay_t<decltype(payload)>,
                                   ast::LetStmt>) {
        const auto local = require_symbol(statement, SymbolKind::Local, id,
                                          payload.binding.name.text);
        if (payload.annotation)
          require_equivalent(symbol_type(local), type(**payload.annotation));
        else
          require_equivalent(symbol_type(local),
                             expression(*payload.initializer).type);
      }
    });
  };
  auto check_constant = [&](const ast::Constant &node,
                            std::optional<SymbolId> owner = {}) {
    auto kind = SymbolKind::Constant;
    auto name = node.name.text;
    if (owner) {
      kind = SymbolKind::AssociatedConstant;
      name = symbol(*owner).name + "::" + name;
    }
    const auto id = require_symbol(node, kind, owner, name);
    require_equivalent(symbol_type(id), type(*node.type));
    require_equivalent(symbol_type(id), expression(*node.value).type);
    (void)constant(id);
  };
  for (const auto &item : crate_->items)
    ast::visit(item, [&](const ast::Node &, const auto &payload) {
      using Payload = std::decay_t<decltype(payload)>;
      if constexpr (std::is_same_v<Payload, ast::Struct>)
        check_struct(payload);
      if constexpr (std::is_same_v<Payload, ast::Function>)
        check_function(payload);
      if constexpr (std::is_same_v<Payload, ast::Constant>)
        check_constant(payload);
      if constexpr (std::is_same_v<Payload, ast::Impl>) {
        const auto target = type(*payload.target);
        if (target->kind() != SemanticType::Kind::Struct)
          throw std::invalid_argument("checked impl target is not a struct");
        const auto owner = target->symbol();
        for (const auto &member : payload.members)
          ast::visit(member, [&](const ast::Node &, const auto &value) {
            if constexpr (std::is_same_v<std::decay_t<decltype(value)>,
                                         ast::Function>)
              check_function(value, owner);
            if constexpr (std::is_same_v<std::decay_t<decltype(value)>,
                                         ast::Constant>)
              check_constant(value, owner);
          });
      }
    });
  if (expected.size() != declarations_.size())
    throw std::invalid_argument(
        "semantic symbols do not match AST declarations");
}
void SemanticIndex::validate_shapes() const {
  using TypeKind = SemanticType::Kind;
  auto expect = [](const SemanticTypePtr &type, TypeKind kind) {
    if (type->kind() != kind)
      throw std::invalid_argument("semantic type does not match AST payload");
  };
  ast::walk(*crate_, [&](const auto &node, const auto &payload) {
    using Owner = std::decay_t<decltype(node)>;
    using Payload = std::decay_t<decltype(payload)>;
    if constexpr (std::is_same_v<Owner, ast::TypePredicate>)
      return ast::WalkControl::SkipChildren;
    if constexpr (std::is_same_v<Owner, ast::TypeSyntax>) {
      const auto &resolved = type(node);
      if constexpr (std::is_same_v<Payload, ast::UnitType>)
        expect(resolved, TypeKind::Unit);
      if constexpr (std::is_same_v<Payload, ast::ParenType>)
        require_equivalent(resolved, type(*payload.inner));
      if constexpr (std::is_same_v<Payload, ast::ReferenceType>) {
        expect(resolved, TypeKind::Ref);
        if (resolved->is_mutable() !=
            (payload.mutability == ast::Mutability::Mutable))
          throw std::invalid_argument("reference type mutability mismatch");
        require_equivalent(resolved->element(), type(*payload.referent));
      }
      if constexpr (std::is_same_v<Payload, ast::ArrayType>) {
        expect(resolved, TypeKind::Array);
        require_equivalent(resolved->element(), type(*payload.element));
        expect(expression(*payload.length).type, TypeKind::Usize);
      }
      if constexpr (std::is_same_v<Payload, ast::PathType>) {
        if (payload.path.segments.empty())
          throw std::invalid_argument("checked type path is empty");
        const auto name = ast::name_text(payload.path.segments.back().name);
        const std::unordered_map<std::string_view, TypeKind> primitives{
            {"bool", TypeKind::Bool},
            {"i32", TypeKind::I32},
            {"u32", TypeKind::U32},
            {"isize", TypeKind::Isize},
            {"usize", TypeKind::Usize}};
        std::vector<const ast::TypeSyntax *> arguments;
        for (const auto &segment : payload.path.segments)
          if (segment.arguments)
            for (const auto &argument : segment.arguments->values)
              if (const auto *syntax = std::get_if<ast::TypePtr>(&argument))
                arguments.push_back(syntax->get());
        if (auto found = primitives.find(name); found != primitives.end()) {
          expect(resolved, found->second);
          if (!arguments.empty())
            throw std::invalid_argument(
                "primitive has unexpected type arguments");
        } else if (name == "Box" || name == "Vec") {
          if (name == "Box")
            expect(resolved, TypeKind::Box);
          else
            expect(resolved, TypeKind::Vec);
          if (arguments.size() != 1)
            throw std::invalid_argument(
                "container type argument count mismatch");
          require_equivalent(resolved->element(), type(*arguments.front()));
        } else {
          expect(resolved, TypeKind::Struct);
          if (name != "Self" && resolved->name() != name)
            throw std::invalid_argument(
                "type path resolves to wrong declaration");
          if (!arguments.empty())
            throw std::invalid_argument("struct has unexpected type arguments");
        }
      }
    }
    if constexpr (std::is_same_v<Owner, ast::Expr>) {
      const auto &fact = expression(node);
      if (fact.binary.has_value() != std::is_same_v<Payload, ast::BinaryExpr> ||
          fact.compound.has_value() !=
              std::is_same_v<Payload, ast::CompoundAssignExpr>)
        throw std::invalid_argument(
            "resolved operation attached to wrong AST kind");
      if (fact.unary.has_value() != std::is_same_v<Payload, ast::UnaryExpr>)
        throw std::invalid_argument(
            "resolved unary attached to wrong AST kind");
      constexpr bool receiver_kind =
          std::is_same_v<Payload, ast::MethodCallExpr> ||
          std::is_same_v<Payload, ast::FieldExpr> ||
          std::is_same_v<Payload, ast::IndexExpr>;
      if (fact.receiver.has_value() != receiver_kind)
        throw std::invalid_argument("receiver use attached to wrong AST kind");
      if (fact.receiver)
        validate_operand(*this, *fact.receiver);
      if (!fact.adjustments.empty())
        validate_operand(*this, OperandSemantics{node.id,
                                                 fact.adjustments.back().target,
                                                 fact.adjustments});
      if constexpr (std::is_same_v<Payload, ast::UnaryExpr>) {
        const auto &unary = *fact.unary;
        constexpr std::array operations{UnaryOperation::Negate,
                                        UnaryOperation::Complement,
                                        UnaryOperation::Dereference};
        if (unary.operand.node != payload.operand->id ||
            unary.operation !=
                operations.at(static_cast<std::size_t>(payload.op)))
          throw std::invalid_argument(
              "resolved unary does not match AST operand");
        validate_operand(*this, unary.operand);
        require_equivalent(fact.type, unary.result);
        const auto &operand_type = unary.operand.effective_type;
        if (unary.operation == UnaryOperation::Dereference) {
          if (operand_type->kind() != TypeKind::Ref &&
              operand_type->kind() != TypeKind::Box)
            throw std::invalid_argument(
                "unary dereference has no referent type");
          require_equivalent(unary.result, operand_type->element());
        } else {
          require_equivalent(unary.result, operand_type);
          const auto kind = operand_type->kind();
          bool valid = kind == TypeKind::I32 || kind == TypeKind::Isize;
          if (unary.operation == UnaryOperation::Complement)
            valid = valid || kind == TypeKind::U32 || kind == TypeKind::Usize ||
                    kind == TypeKind::Bool;
          if (!valid)
            throw std::invalid_argument(
                "unary operator is invalid for operand type");
        }
      }
      if constexpr (std::is_same_v<Payload, ast::BinaryExpr>) {
        validate_operation(*this, *fact.binary, *payload.left, *payload.right,
                           payload.op);
        require_equivalent(fact.type, fact.binary->result);
      }
      if constexpr (std::is_same_v<Payload, ast::CompoundAssignExpr>) {
        validate_operation(*this, *fact.compound, *payload.target,
                           *payload.value, binary_operation(payload.op));
        require_equivalent(fact.compound->result,
                           expression(*payload.target).type);
      }
      if constexpr (std::is_same_v<Payload, ast::CallExpr> ||
                    std::is_same_v<Payload, ast::MethodCallExpr>) {
        if (!fact.callable)
          throw std::invalid_argument("checked call has no resolved target");
        if (const auto *target = std::get_if<FunctionTarget>(&*fact.callable)) {
          const auto &signature = function(target->symbol);
          require_equivalent(fact.type, signature.result);
          std::vector<SemanticTypePtr> parameters;
          if constexpr (std::is_same_v<Payload, ast::CallExpr>)
            if (signature.receiver)
              parameters.push_back(signature.receiver->type);
          for (const auto &parameter : signature.parameters)
            parameters.push_back(parameter.type);
          if (parameters.size() != payload.arguments.size())
            throw std::invalid_argument(
                "call argument count differs from signature");
          for (std::size_t index = 0; index < parameters.size(); ++index) {
            const auto &argument = expression(*payload.arguments[index]);
            auto effective = argument.type;
            if (!argument.adjustments.empty())
              effective = argument.adjustments.back().target;
            require_equivalent(effective, parameters[index]);
          }
        }
      }
      if constexpr (std::is_same_v<Payload, ast::CallExpr>) {
        const auto &callee = expression(*payload.callee);
        if (!callee.callable || !same_target(*fact.callable, *callee.callable))
          throw std::invalid_argument(
              "call target does not match callee identity");
      }
      if constexpr (std::is_same_v<Payload, ast::MethodCallExpr>) {
        if (fact.receiver->node != payload.receiver->id)
          throw std::invalid_argument(
              "method receiver use points to wrong AST node");
        if (const auto *target = std::get_if<FunctionTarget>(&*fact.callable)) {
          const auto &record = symbol(target->symbol);
          auto receiver = expression(*payload.receiver).type;
          while (receiver->kind() == K::Ref || receiver->kind() == K::Box)
            receiver = receiver->element();
          if (receiver->kind() != K::Struct ||
              record.owner != receiver->symbol() ||
              record.name != receiver->name() + "::" + payload.method.text ||
              !function(target->symbol).receiver)
            throw std::invalid_argument(
                "method target does not match receiver");
          require_equivalent(fact.receiver->effective_type,
                             function(target->symbol).receiver->type);
        } else {
          const auto builtin_target =
              std::get<BuiltinTarget>(*fact.callable).builtin;
          const auto name = payload.method.text;
          const bool matches =
              (name == "clone" && builtin_target == BuiltinId::Clone) ||
              (name == "len" && builtin_target == BuiltinId::Len) ||
              (name == "is_empty" && builtin_target == BuiltinId::IsEmpty) ||
              (name == "push" && builtin_target == BuiltinId::Push) ||
              (name == "remove" && builtin_target == BuiltinId::Remove);
          if (!matches)
            throw std::invalid_argument("builtin method target name mismatch");
        }
      }
      if (fact.type->kind() == K::Function) {
        const auto *target = fact.callable
                                 ? std::get_if<FunctionTarget>(&*fact.callable)
                                 : nullptr;
        if (!target || target->symbol != fact.type->symbol())
          throw std::invalid_argument(
              "function expression target identity mismatch");
      }
      if (fact.type->kind() == K::BuiltinFunction) {
        const auto *target = fact.callable
                                 ? std::get_if<BuiltinTarget>(&*fact.callable)
                                 : nullptr;
        const auto &builtin = *get_if<BuiltinFunctionType>(*fact.type);
        if (!target || target->builtin != builtin.builtin)
          throw std::invalid_argument(
              "builtin expression target identity mismatch");
      }
      if constexpr (std::same_as<P, ast::IntegerExpr>) {
        const auto kind = fact.type->kind();
        if (kind != K::I32 && kind != K::U32 && kind != K::Isize &&
            kind != K::Usize)
          throw std::invalid_argument(
              "integer expression has non-integer type");
      }
      if constexpr (std::same_as<P, ast::BooleanExpr>)
        expect(fact.type, K::Bool);
      if constexpr (std::same_as<P, ast::UnitExpr> ||
                    std::same_as<P, ast::AssignExpr> ||
                    std::same_as<P, ast::CompoundAssignExpr> ||
                    std::same_as<P, ast::WhileExpr>)
        expect(fact.type, K::Unit);
      if constexpr (std::same_as<P, ast::BreakExpr> ||
                    std::same_as<P, ast::ContinueExpr> ||
                    std::same_as<P, ast::ReturnExpr>)
        expect(fact.type, K::Never);
      if constexpr (std::same_as<P, ast::BorrowExpr>) {
        expect(fact.type, K::Ref);
        if (fact.type->is_mutable() !=
            (payload.mutability == ast::Mutability::Mutable))
          throw std::invalid_argument("borrow mutability fact mismatch");
        require_equivalent(fact.type->element(),
                           expression(*payload.operand).type);
      }
      if constexpr (std::same_as<P, ast::CastExpr>)
        require_equivalent(fact.type, type(*payload.target_type));
      if constexpr (std::same_as<P, ast::FieldExpr>) {
        if (!fact.symbol || symbol(*fact.symbol).kind != SymbolKind::Field)
          throw std::invalid_argument("field expression has no field symbol");
        require_equivalent(fact.type, symbol_type(*fact.symbol));
        if (fact.receiver->node != payload.base->id ||
            fact.receiver->effective_type->kind() != K::Struct ||
            symbol(*fact.symbol).owner !=
                fact.receiver->effective_type->symbol())
          throw std::invalid_argument("field receiver use identity mismatch");
      }
      if constexpr (std::same_as<P, ast::IndexExpr>) {
        if (fact.receiver->node != payload.base->id)
          throw std::invalid_argument(
              "index receiver use points to wrong AST node");
        const auto &container = fact.receiver->effective_type;
        if (container->kind() != K::Array && container->kind() != K::Vec)
          throw std::invalid_argument("index receiver use is not a container");
        require_equivalent(fact.type, container->element());
        expect(expression(*payload.index).type, K::Usize);
      }
      if constexpr (std::same_as<P, ast::ArrayExpr>) {
        expect(fact.type, K::Array);
        if (fact.type->length() != payload.elements.size())
          throw std::invalid_argument("array expression length fact mismatch");
      }
      if constexpr (std::same_as<P, ast::RepeatArrayExpr>)
        expect(fact.type, K::Array);
      if constexpr (std::same_as<P, ast::StructExpr>) {
        expect(fact.type, K::Struct);
        if (fact.symbol != fact.type->symbol())
          throw std::invalid_argument("struct expression identity mismatch");
      }
    }
    return ast::WalkControl::Continue;
  });
}
} // namespace rx
