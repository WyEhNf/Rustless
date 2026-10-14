#pragma once

#include "rx/ast/visitor.hpp"
#include "rx/semantic/safety/safe_gate.hpp"

#include <array>
#include <functional>
#include <stdexcept>
#include <unordered_set>

namespace rx::safety_detail {

void require_equivalent(const SemanticTypePtr &left,
                        const SemanticTypePtr &right);
ast::BinaryOp source_operator(const ResolvedOperation &operation);
void validate_operand(const SafeGate &index, const OperandSemantics &operand);
void validate_operation(const SafeGate &index, const ResolvedBinary &fact,
                        const ast::Expr &left, const ast::Expr &right,
                        ast::BinaryOp op);
bool same_target(const CallTarget &left, const CallTarget &right);

} // namespace rx::safety_detail
