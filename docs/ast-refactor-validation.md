# AST refactor validation

Validated on 2026-10-11 with the existing MinGW C++23 build.
The migration contains 30 commits after baseline
0a2aea328a24f66f07400b07899d091614153647.

## Delivered interfaces

- Expr, TypeSyntax and Stmt use dedicated variant payloads.
- Required recursive children use move-only, checked Box ownership.
- Binary, unary and compound assignment operators are distinct enums.
- Boolean literals use bool; integer literals retain base and suffix.
- Paths retain generic arguments on individual segments.
- References retain lifetimes and mutability; parentheses remain explicit.
- Array lengths and repetition counts retain expression structure.
- Blocks separate statements from optional value tails.
- Function signatures preserve receivers and optional return annotations.
- Use trees preserve absolute paths, aliases, wildcards and groups.
- Declarations preserve generic bounds, where clauses and derive attributes.
- Impl members preserve their original order.
- Visitors cover all variants; the walker visits each identity once.
- Source spans carry FileId and UTF-8 byte offsets.
- Semantic SymbolId is distinct from NodeId and records its declaration.

## Checks completed

| Check | Result |
| --- | --- |
| CMake build | Passed |
| CTest, including AST shape and semantic coercion regressions | 6/6 |
| External lex manifests | 53/53 |
| External parser manifests | 442/442 |
| External semantic manifests | 236/236 |
| Accepted parser crate AST traversal, identities, spans and printing | 47/47 |
| Semantic corpus AST traversal, identities, spans and printing | 236/236 |
| Standalone type and expression header construction | Passed |
| Diff whitespace checks | Passed |

Parser crate samples were selected from manifest entries with entry=crate
and compilation_success=true. Fragment entries remain parser tests.
The corpus walker rejects duplicate/zero identities and invalid file spans.
Shape tests check paths, imports, constraints, operator precedence,
parentheses, borrow nesting, block tails and mixed impl member order.

UTF-8 mapping is tested directly through SourceFile. Rx currently permits
ASCII comments; parsing non-ASCII comment content is checked as a rejection.
Parenthesis coercion tests ensure conversions are not recorded twice.

## Reproduction

Run cmake --build build -j 4, then ctest --test-dir build --output-on-failure.
Run scripts/test_frontend.py with build/rxc.exe and ../rx-compiler-testcases.
Run build/frontend_ast_corpus.exe ../rx-compiler-testcases/semantic --semantic.
On Windows, place the configured compiler runtime directory on PATH.

## Compatibility

AST consumers must use payload visitors or get_if<T>, named child fields
and operator enums. AST dump names now expose the dedicated node kinds.
Use and lifetime constraints retain syntax for future semantic resolution.
Their interpretation remains within the compiler's existing semantic scope.
The current IR infrastructure passes ir_smoke and has no AST lowering pass.
The user's four existing frontend comment deletions remain uncommitted.
