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

## Consumer interfaces

Keep the AST, result and index alive and unchanged while consuming semantic facts:

```cpp
auto parsed = rx::parse_source(source, diagnostics);
auto checked = rx::analyze(*parsed.crate, diagnostics);
rx::SemanticIndex semantic(*parsed.crate, checked);
rx::ir::SemanticBridge bridge(semantic);
auto declarations = bridge.declarations();
```

`SemanticIndex` rejects failed analysis, results bound to another crate,
missing expression/type facts, duplicate identities and invalid declaration links.
`NodeId`, `SymbolId`, `ValueId` and `BlockId` have separate types. Symbol allocation
is independent of AST numbering. Declarations, parameters and instructions retain
optional semantic links or source provenance for later diagnostics.

`SemanticBridge` exports function ABI, source-order struct fields and checked
RV32 layouts. Bool occupies one byte in memory and uses i1 as an SSA value.
Integers and pointers occupy four bytes. Vec occupies twelve bytes. Arrays use
element layout; struct fields and final size are padded to their alignment.
Layout overflow and inline recursive aggregates are rejected.

The internal aggregate ABI passes arrays, structs and Vec by address. An aggregate
return becomes void with a hidden output address before the receiver and ordinary
parameters. Unit parameters occupy no machine argument but remain represented in
`FunctionAbi`. This is an internal compiler convention, not a platform C ABI claim.
Associated functions use their resolved names such as `Packet::get`; LLVM names
are quoted and escaped when necessary.

`binary(owner, payload)` and `compound(owner, payload)` consume concrete AST
payloads and checked operand types. Signed division/remainder/right-shift and
ordering select signed IR operators; unsigned operands select unsigned operators.
Logical operators produce `ShortCircuit` descriptors so callers must build control
flow. Aggregate equality needs a future aggregate lowering path. These descriptors
do not emit loads, adjustments or instructions on their own.

## Instruction construction

```cpp
rx::ir::Function function{"example", rx::ir::Type::I32};
rx::ir::Builder builder(function);
builder.select(builder.create_block("entry"));
builder.set_origin(expression);
auto result = builder.binary(rx::ir::BinaryOp::Add,
    rx::ir::Operand::constant(rx::ir::Type::I32, 1),
    rx::ir::Operand::constant(rx::ir::Type::I32, 2));
builder.ret(result);
rx::ir::Module module{{}, {std::move(function)}};
auto errors = rx::ir::verify(module);
auto llvm = rx::ir::print(module); // refuses invalid modules
```

Instructions and operands use closed variants. `ir::visit` checks full payload
coverage and identical return types at compile time. Opcode and result traits are
derived from payloads. The builder checks local instruction contracts and insertion
order; module verification checks targets, call signatures, Phi predecessors,
SSA uniqueness, use-before-definition and dominance. Direct payload construction
is supported, so callers must still run module verification before using it.
