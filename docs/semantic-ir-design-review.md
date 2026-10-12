# Semantic and IR design review

Date: 2026-10-12. Baseline: `082a2bf`.

## Representation and phase boundaries

- Semantic types are immutable, privately constructed variant values. `TypeContext`
  creates and canonicalizes them; callers cannot change an array into a reference
  or attach an element type to a scalar after construction.
- Nominal types use `SymbolId`. Cross-context nominal comparison requires the
  same declaration identity domain; IDs from independent crates are not globally
  unique. The current command-line analysis processes one crate at a time.
- Strict type equality is separate from diagnostic recovery through Error.
  Canonicalization uses an ordered map and an ownership membership set; it does
  not promise constant-time structural type lookup.
- Binary, compound and unary facts contain resolved operations and ordered input
  adjustments. Method, field and index receivers have separate use-site facts.
  A receiver conversion is not a conversion of the resulting expression.
- Mutable-to-shared and never conversions describe expression uses without
  changing the expression's intrinsic type. Parentheses preserve their expected
  context while recording a use conversion only once.
- A successful semantic draft is checked before it becomes a frozen snapshot.
  `SemanticIndex` keeps that snapshot alive and borrows the unchanged AST.
  Object queries check both identity and object provenance; temporary ASTs are
  rejected by the public construction interfaces.
- A function alias retains its resolved declaration identity. Builtins have
  language-level enum identities, including container-bound constructor types.
  The index checks user function argument, receiver and result signatures.
- The IR bridge consumes resolved operations and call targets. It does not
  repeat spelling-based name lookup or source operator classification.
- RV32 layout and shift masking are explicit target policies. Aggregate equality
  is a structural operation descriptor rather than pointer equality. Aggregate
  passing is an internal convention distinct from the platform C ABI.
- Phi validation counts incoming CFG edges, including parallel edges, and checks
  consistent values for repeated predecessor blocks.

## Verification

The final build includes the temporary-AST lifetime guards and the explicit
dereference use-site regression. The following checks passed:

| Check | Result |
| --- | --- |
| MinGW C++23 CMake build | Passed |
| CTest | 14/14 |
| External lexer manifest | 53/53 |
| External parser manifest | 442/442 |
| External semantic manifest | 236/236 |
| Final semantic AST and fact corpus | 236/236 |
| LLVM parallel-edge Phi fixture, Clang RV32IM verification | Passed |

The manifest report is `build/design-review/frontend-alignment-final.json`.
The final semantic corpus was rerun after the lifetime and dereference fixes.
Generated reports and compiler fixtures remain under ignored `build/`.

The focused tests cover immutable type shape and identity, illegal operator
domains, ordered adjustments, alias calls, frozen snapshot lifetime, altered
facts, foreign ASTs, aggregate ABI plans, verifier failures and LLVM printing.
Passing these checks does not establish source-program execution or performance.

## Remaining implementation

The bridge provides lowering plans. Full expression/statement instruction
generation, runtime containers, ownership destruction, optimization passes and
machine code generation are still future work. The tutorial distinguishes
current project excerpts from the algorithms and interfaces proposed for that
work. Its assembled TeX is produced by `python scripts/build_tutorial.py`.
