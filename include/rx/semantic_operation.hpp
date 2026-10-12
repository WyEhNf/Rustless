#pragma once

#include "rx/ast/node.hpp"
#include "rx/builtin.hpp"
#include "rx/semantic_type.hpp"
#include "rx/ast/operators.hpp"
#include <optional>
#include <variant>
#include <vector>

namespace rx {
struct Adjustment {
  // Implicit operations are stored in execution order.
  enum class Kind {
    Dereference, BorrowShared, BorrowMutable, MutToShared, NeverToAny
  } kind{Kind::Dereference};
  SemanticTypePtr target;
};

// A use records conversions without changing the expression's intrinsic type.
struct OperandSemantics {
  ast::NodeId node;
  SemanticTypePtr effective_type;
  std::vector<Adjustment> adjustments;
};
enum class ArithmeticOp { Add, Sub, Mul, Div, Rem };
enum class BitwiseOp { And, Or, Xor };
enum class ShiftDirection { Left, Right };
enum class ShiftCountPolicy { MaskToWidth };
enum class Relation { Eq, Ne, Lt, Le, Gt, Ge };
enum class EqualityOp { Eq, Ne };
enum class LogicalOp { And, Or };
struct IntegerArithmetic { ArithmeticOp op; };
struct BitwiseOperation { BitwiseOp op; };
struct ShiftOperation {
  ShiftDirection direction;
  ShiftCountPolicy count_policy{ShiftCountPolicy::MaskToWidth};
};
struct ScalarComparison { Relation relation; };
struct StructuralEquality { EqualityOp op; };
struct LogicalOperation { LogicalOp op; };
using ResolvedOperation =
    std::variant<IntegerArithmetic, BitwiseOperation, ShiftOperation,
                 ScalarComparison, StructuralEquality, LogicalOperation>;
struct ResolvedBinary {
  ResolvedOperation operation;
  OperandSemantics left;
  OperandSemantics right;
  SemanticTypePtr result;
};
enum class UnaryOperation { Negate, Complement, Dereference };
struct ResolvedUnary {
  UnaryOperation operation;
  OperandSemantics operand;
  SemanticTypePtr result;
};
struct FunctionTarget { SymbolId symbol; };
struct BuiltinTarget { BuiltinId builtin; };
using CallTarget = std::variant<FunctionTarget, BuiltinTarget>;
// Resolve once in the language layer; backends consume the selected operation.
std::optional<ResolvedOperation> resolve_operation(
    ast::BinaryOp op, const SemanticTypePtr &left,
    const SemanticTypePtr &right);
ast::BinaryOp binary_operation(ast::CompoundAssignOp op);
} // namespace rx
