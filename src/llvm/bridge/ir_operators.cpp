#include "rx/llvm/bridge/semantic.hpp"
#include <stdexcept>

namespace rx::ir {
namespace {
bool signed_integer(const SemanticTypePtr &type) {
  if (!type) {
    throw std::invalid_argument("missing resolved operand type");
  }
  return type->kind() == SemanticType::Kind::I32 || type->kind() == SemanticType::Kind::Isize;
}
BinaryOp arithmetic(ArithmeticOp op, bool is_signed) {
  switch (op) {
  case ArithmeticOp::Add:
    return BinaryOp::Add;
  case ArithmeticOp::Sub:
    return BinaryOp::Sub;
  case ArithmeticOp::Mul:
    return BinaryOp::Mul;
  case ArithmeticOp::Div:
    if (is_signed) {
      return BinaryOp::SDiv;
    }
    return BinaryOp::UDiv;
  case ArithmeticOp::Rem:
    if (is_signed) {
      return BinaryOp::SRem;
    }
    return BinaryOp::URem;
  }
  throw std::invalid_argument("invalid resolved arithmetic operation");
}
BinaryOp bitwise(BitwiseOp op) {
  switch (op) {
  case BitwiseOp::And:
    return BinaryOp::And;
  case BitwiseOp::Or:
    return BinaryOp::Or;
  case BitwiseOp::Xor:
    return BinaryOp::Xor;
  }
  throw std::invalid_argument("invalid resolved bitwise operation");
}
Predicate comparison(Relation relation, bool is_signed) {
  switch (relation) {
  case Relation::Eq:
    return Predicate::Eq;
  case Relation::Ne:
    return Predicate::Ne;
  case Relation::Lt:
    if (is_signed) {
      return Predicate::SLt;
    }
    return Predicate::ULt;
  case Relation::Le:
    if (is_signed) {
      return Predicate::SLe;
    }
    return Predicate::ULe;
  case Relation::Gt:
    if (is_signed) {
      return Predicate::SGt;
    }
    return Predicate::UGt;
  case Relation::Ge:
    if (is_signed) {
      return Predicate::SGe;
    }
    return Predicate::UGe;
  }
  throw std::invalid_argument("invalid resolved comparison");
}
ShortCircuit short_circuit(LogicalOp op) {
  switch (op) {
  case LogicalOp::And:
    return ShortCircuit::And;
  case LogicalOp::Or:
    return ShortCircuit::Or;
  }
  throw std::invalid_argument("invalid resolved logical operation");
}
}
BinaryLowering SemanticBridge::lower_binary(const ResolvedBinary &resolved) const {
  const auto operand = scalar_type(resolved.left.effective_type);
  const auto result = scalar_type(resolved.result);
  if (!result || *result == Type::Void) {
    throw std::invalid_argument("resolved operation has no scalar result");
  }
  const bool is_signed = signed_integer(resolved.left.effective_type);
  auto require_scalar = [&](bool integer_only = false) {
    if (!operand || *operand == Type::Void || (integer_only && *operand != Type::I32)) {
      throw std::invalid_argument("resolved scalar operand type is invalid");
    }
    return *operand;
  };
  auto operation = std::visit(
      ast::Overloaded{
          [&](const IntegerArithmetic &payload) -> LoweredOperation {
            return ScalarArithmetic{arithmetic(payload.op, is_signed), require_scalar(true), {}};
          },
          [&](const BitwiseOperation &payload) -> LoweredOperation {
            const auto type = require_scalar();
            if (type != Type::I1 && type != Type::I32) {
              throw std::invalid_argument("bitwise operands must be integers");
            }
            return ScalarArithmetic{bitwise(payload.op), type, {}};
          },
          [&](const ShiftOperation &payload) -> LoweredOperation {
            if (payload.count_policy != ShiftCountPolicy::MaskToWidth) {
              throw std::invalid_argument("unsupported shift count policy");
            }
            BinaryOp op;
            switch (payload.direction) {
            case ShiftDirection::Left:
              op = BinaryOp::Shl;
              break;
            case ShiftDirection::Right:
              if (is_signed) {
                op = BinaryOp::AShr;
              } else {
                op = BinaryOp::LShr;
              }
              break;
            default:
              throw std::invalid_argument("invalid shift direction");
            }
            return ScalarArithmetic{op, require_scalar(true), target_.shift_count_mask()};
          },
          [&](const rx::ScalarComparison &payload) -> LoweredOperation {
            return ScalarComparison{comparison(payload.relation, is_signed), require_scalar()};
          },
          [&](const rx::StructuralEquality &payload) -> LoweredOperation {
            switch (payload.op) {
            case EqualityOp::Eq:
              return StructuralEquality{resolved.left.effective_type, false};
            case EqualityOp::Ne:
              return StructuralEquality{resolved.left.effective_type, true};
            }
            throw std::invalid_argument("invalid structural equality");
          },
          [&](const LogicalOperation &payload) -> LoweredOperation {
            if (require_scalar() != Type::I1) {
              throw std::invalid_argument("logical operands must be bool");
            }
            return short_circuit(payload.op);
          },
      },
      resolved.operation);
  return {std::move(operation), resolved.left, resolved.right, *result};
}
BinaryLowering SemanticBridge::binary(const ast::Expr &node, const ast::BinaryExpr &payload) const {
  if (std::get_if<ast::BinaryExpr>(&node.data) != &payload) {
    throw std::invalid_argument("binary payload does not belong to this AST node");
  }
  const auto &resolved = semantic_.expression(node).binary;
  if (!resolved) {
    throw std::invalid_argument("binary expression lacks a resolved operation");
  }
  return lower_binary(*resolved);
}
CompoundLowering SemanticBridge::compound(const ast::Expr &node,
                                          const ast::CompoundAssignExpr &payload) const {
  if (std::get_if<ast::CompoundAssignExpr>(&node.data) != &payload) {
    throw std::invalid_argument("assignment payload does not belong to this AST node");
  }
  const auto &resolved = semantic_.expression(node).compound;
  if (!resolved) {
    throw std::invalid_argument("assignment lacks a resolved operation");
  }
  auto lowered = lower_binary(*resolved);
  const auto *operation = std::get_if<ScalarArithmetic>(&lowered.operation);
  if (!operation) {
    throw std::invalid_argument("assignment requires scalar arithmetic");
  }
  return {*operation, std::move(lowered.left), std::move(lowered.right)};
}
UnaryLowering SemanticBridge::unary(const ast::Expr &node, const ast::UnaryExpr &payload) const {
  if (std::get_if<ast::UnaryExpr>(&node.data) != &payload) {
    throw std::invalid_argument("unary payload does not belong to this AST node");
  }
  const auto &resolved = semantic_.expression(node).unary;
  if (!resolved) {
    throw std::invalid_argument("unary expression lacks a resolved operation");
  }
  if (resolved->operation == UnaryOperation::Dereference) {
    return {DereferencePlace{resolved->result, layout(resolved->result)},
            resolved->operand,
            resolved->result};
  }
  const auto type = scalar_type(resolved->operand.effective_type);
  if (!type || (*type != Type::I1 && *type != Type::I32)) {
    throw std::invalid_argument("unary operation requires an integer or bool");
  }
  ScalarUnary operation;
  switch (resolved->operation) {
  case UnaryOperation::Negate:
    if (*type != Type::I32) {
      throw std::invalid_argument("negation requires an integer");
    }
    operation = {BinaryOp::Sub, *type, 0};
    break;
  case UnaryOperation::Complement: {
    std::uint64_t mask = 1;
    if (*type != Type::I1) {
      mask = (std::uint64_t{1} << target_.integer_bits()) - 1;
    }
    operation = {BinaryOp::Xor, *type, static_cast<std::uint32_t>(mask)};
    break;
  }
  default:
    throw std::invalid_argument("invalid resolved unary operation");
  }
  return {operation, resolved->operand, resolved->result};
}
}
