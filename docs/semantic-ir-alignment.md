# Semantic and IR alignment

Baseline: d205c69. Work is committed in small feature/fix slices, with at
least 40 commits. Existing user edits in frontend.cpp are not part of this work.

## Semantic consumers

Expression handlers receive the identity-bearing Expr and the specific
payload selected by ast::visit. Helpers must not repeat a std::get on an
untyped ExprPtr after dispatch. Ownership stays in the AST; analysis borrows
read-only references and stores facts by NodeId.

Literal interpretation consumes IntegerLiteral.base and suffix. Symbol kinds
are enums. Semantic exports include declaration types and function signatures,
so an IR consumer does not repeat name resolution or type inference.
An indexed semantic view checks identities, required facts and links before
the IR bridge consumes them. Results of rejected analysis are not checked input.

## Existing IR

The implemented instruction set and LLVM text format are retained. ValueId
and BlockId become distinct types. Construction helpers name their operands
and enforce instruction-specific contracts. Validation checks types, control
flow, SSA definitions and call signatures before output.

Instruction provenance retains an optional syntax NodeId and Span. Declaration
links use SymbolId, never a cast from syntax identity. Semantic-to-IR utilities
translate RV32 scalar types, aggregate layouts and function signatures using
checked semantic information and typed AST visits.

The existing backend has no expression lowering pass. This update establishes
and tests its input and construction interfaces; it does not claim executable
code generation for the entire language.

## Verification

- Keep existing frontend, semantic and IR smoke tests passing.
- Add focused regressions for typed payload consumers and semantic exports.
- Add negative IR construction/validation cases and stable printer checks.
- Exercise the bridge on parsed and checked declarations, including impl items.
- Run the external lexer, parser and semantic manifests.
- Check AST corpus identities/spans and semantic fact completeness.
- Commit each completed module, debugging fix and verification result.

The AST uses variant visitation, not virtual accept-based double dispatch.
Semantic recursion still manages expected types, lexical scopes and loop state.
IR alignment must not replace those responsibilities with a generic tree walk.
