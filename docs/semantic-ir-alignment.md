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

## Completed verification (2026-10-11)

This integration was completed in 58 commits after baseline `d205c69`, including
the verification record. Feature/fix commits cover the semantic migration,
checked facts, typed IR model, visitors, verifier, builder and bridge. Existing
frontend.cpp comment edits remain uncommitted and were not included.

| Check | Result |
| --- | --- |
| MinGW C++23 build | Passed |
| CTest, including semantic and IR alignment regressions | 10/10 |
| rx-compiler-testcases lexer manifests | 53/53 |
| rx-compiler-testcases parser manifests | 442/442 |
| rx-compiler-testcases semantic manifests | 236/236 |
| Semantic AST corpus: identities, spans, facts and links | 236/236 |
| Accepted parser crate AST corpus | 47/47 |
| LLVM/Clang RV32IM compilation of emitted IR fixtures | 2/2 |
| Committed diff whitespace check | Passed |

The Clang checks compile the migrated instruction smoke fixture and the builder
diamond/Phi fixture (including a qualified function name) to RV32 objects.
They do not test execution of source-language programs. Clang reported its normal
target-triple normalization warning, with no compilation errors.

Reproduce from the Rustless directory after configuring the build:

```powershell
$env:PATH = 'E:\mingw64\bin;' + $env:PATH
cmake --build build -j 4
ctest --test-dir build --output-on-failure
python scripts/test_frontend.py --compiler build/rxc.exe `
  --test-root ../rx-compiler-testcases `
  --report build/semantic-ir-alignment-report.json
build/frontend_ast_corpus.exe ../rx-compiler-testcases/semantic --semantic
build/frontend_ast_corpus.exe build/ast-refactor-parser-crates
build/ir_smoke.exe build/typed-ir-smoke.ll
build/ir_alignment.exe build/typed-ir-alignment.ll
& 'C:\Program Files\LLVM\bin\clang.exe' --target=riscv32-unknown-elf `
  -march=rv32im -mabi=ilp32 -x ir -c build/typed-ir-smoke.ll `
  -o build/typed-ir-smoke.o
& 'C:\Program Files\LLVM\bin\clang.exe' --target=riscv32-unknown-elf `
  -march=rv32im -mabi=ilp32 -x ir -c build/typed-ir-alignment.ll `
  -o build/typed-ir-alignment.o
git rev-list --count d205c69..HEAD
git diff --check d205c69..HEAD
```

The accepted-parser-crates directory is an existing local extraction of successful
crate-entry parser tests; the complete parser manifest runner above does not
depend on it. Reports and LLVM fixtures are generated under ignored `build/`.

Remaining backend work is full AST expression/statement lowering, application of
semantic adjustments, aggregate operations and runtime integration. The CLI still
provides its existing lex/parse/semantic stages. The new bridge APIs are available
to that future lowering pass and have no undefined placeholder methods.
