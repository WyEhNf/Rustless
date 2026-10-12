#pragma once

#include "rx/ast/base.hpp"
#include "rx/semantic_type.hpp"
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
struct FunctionTarget { SymbolId symbol; };
struct BuiltinTarget { BuiltinId builtin; };
using CallTarget = std::variant<FunctionTarget, BuiltinTarget>;
} // namespace rx
