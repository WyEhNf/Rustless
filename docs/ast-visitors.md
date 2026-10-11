# AST visitors

Visitors receive two read-only references: the identity-bearing owner and
its concrete payload. For expressions this is `(const Expr&, const BinaryExpr&)`;
for declarations it is `(const Function&, const Function&)`. A `const Node&`
first argument accepts any owner when only its identity or span is needed.
The owner and payload always refer to the same syntax node.

`ExhaustiveVisitor<V, T>` checks every alternative and requires identical
return types. Missing handlers, mutable-only handlers and inconsistent return
types fail this concept. Generic fallbacks explicitly opt out of checking
special handling for every alternative. Item, ImplMember and WherePredicate
dispatch to their concrete declaration or predicate owners.

The tree remains closed and read-only. Adding a node requires adding its
child enumeration and handlers in exhaustive visitors. There is no virtual
base visitor or numeric kind switch.

## Initial verification

The `ast_visitor` CTest passed after adding compile-time coverage checks for
all union families, rejected handler signatures, and runtime owner/payload
identity checks. Reconfigure CMake after introducing the new test target:

```
cmake -S . -B build
cmake --build build --target ast_visitor -j 4
ctest --test-dir build -R "^ast_visitor$" --output-on-failure
```
