#include "checks.hpp"

namespace rx {

using safety_detail::require_equivalent;
using safety_detail::same_target;
using safety_detail::validate_operand;
using safety_detail::validate_operation;

void SafeGate::validate_shapes() const {
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
          while (receiver->kind() == TypeKind::Ref ||
                 receiver->kind() == TypeKind::Box)
            receiver = receiver->element();
          if (receiver->kind() != TypeKind::Struct ||
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
      if (fact.type->kind() == TypeKind::Function) {
        const FunctionTarget *target = nullptr;
        if (fact.callable)
          target = std::get_if<FunctionTarget>(&*fact.callable);
        if (!target || target->symbol != fact.type->symbol())
          throw std::invalid_argument(
              "function expression target identity mismatch");
      }
      if (fact.type->kind() == TypeKind::BuiltinFunction) {
        const BuiltinTarget *target = nullptr;
        if (fact.callable)
          target = std::get_if<BuiltinTarget>(&*fact.callable);
        const auto &builtin = *get_if<BuiltinFunctionType>(*fact.type);
        if (!target || target->builtin != builtin.builtin)
          throw std::invalid_argument(
              "builtin expression target identity mismatch");
      }
      if constexpr (std::is_same_v<Payload, ast::IntegerExpr>) {
        const auto kind = fact.type->kind();
        if (kind != TypeKind::I32 && kind != TypeKind::U32 &&
            kind != TypeKind::Isize && kind != TypeKind::Usize)
          throw std::invalid_argument(
              "integer expression has non-integer type");
      }
      if constexpr (std::is_same_v<Payload, ast::BooleanExpr>)
        expect(fact.type, TypeKind::Bool);
      if constexpr (std::is_same_v<Payload, ast::UnitExpr> ||
                    std::is_same_v<Payload, ast::AssignExpr> ||
                    std::is_same_v<Payload, ast::CompoundAssignExpr> ||
                    std::is_same_v<Payload, ast::WhileExpr>)
        expect(fact.type, TypeKind::Unit);
      if constexpr (std::is_same_v<Payload, ast::BreakExpr> ||
                    std::is_same_v<Payload, ast::ContinueExpr> ||
                    std::is_same_v<Payload, ast::ReturnExpr>)
        expect(fact.type, TypeKind::Never);
      if constexpr (std::is_same_v<Payload, ast::BorrowExpr>) {
        expect(fact.type, TypeKind::Ref);
        if (fact.type->is_mutable() !=
            (payload.mutability == ast::Mutability::Mutable))
          throw std::invalid_argument("borrow mutability fact mismatch");
        require_equivalent(fact.type->element(),
                           expression(*payload.operand).type);
      }
      if constexpr (std::is_same_v<Payload, ast::CastExpr>)
        require_equivalent(fact.type, type(*payload.target_type));
      if constexpr (std::is_same_v<Payload, ast::FieldExpr>) {
        if (!fact.symbol || symbol(*fact.symbol).kind != SymbolKind::Field)
          throw std::invalid_argument("field expression has no field symbol");
        require_equivalent(fact.type, symbol_type(*fact.symbol));
        if (fact.receiver->node != payload.base->id ||
            fact.receiver->effective_type->kind() != TypeKind::Struct ||
            symbol(*fact.symbol).owner !=
                fact.receiver->effective_type->symbol())
          throw std::invalid_argument("field receiver use identity mismatch");
      }
      if constexpr (std::is_same_v<Payload, ast::IndexExpr>) {
        if (fact.receiver->node != payload.base->id)
          throw std::invalid_argument(
              "index receiver use points to wrong AST node");
        const auto &container = fact.receiver->effective_type;
        if (container->kind() != TypeKind::Array &&
            container->kind() != TypeKind::Vec)
          throw std::invalid_argument("index receiver use is not a container");
        require_equivalent(fact.type, container->element());
        expect(expression(*payload.index).type, TypeKind::Usize);
      }
      if constexpr (std::is_same_v<Payload, ast::ArrayExpr>) {
        expect(fact.type, TypeKind::Array);
        if (fact.type->length() != payload.elements.size())
          throw std::invalid_argument("array expression length fact mismatch");
      }
      if constexpr (std::is_same_v<Payload, ast::RepeatArrayExpr>)
        expect(fact.type, TypeKind::Array);
      if constexpr (std::is_same_v<Payload, ast::StructExpr>) {
        expect(fact.type, TypeKind::Struct);
        if (fact.symbol != fact.type->symbol())
          throw std::invalid_argument("struct expression identity mismatch");
      }
    }
    return ast::WalkControl::Continue;
  });
}

} // namespace rx
