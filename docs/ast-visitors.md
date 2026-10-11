# AST visitors

Visitors receive two read-only references: the identity-bearing owner and
its concrete payload. For expressions this is `(const Expr&, const BinaryExpr&)`;
for declarations it is `(const Function&, const Function&)`. A `const Node&`
first argument accepts any owner when only its identity or span is needed.
The owner and payload always refer to the same syntax node.
Include `rx/ast.hpp` for `visit`, or `rx/ast/visitor.hpp` for `walk`.
`visit` dispatches one node; it does not recurse automatically.

`ExhaustiveVisitor<V, T>` checks every alternative and requires identical
return types. Missing handlers, mutable-only handlers and inconsistent return
types fail this concept. Generic fallbacks explicitly opt out of checking
special handling for every alternative. Item, ImplMember and WherePredicate
dispatch to their concrete declaration or predicate owners.

The tree remains closed and read-only. Adding a node requires adding its
child enumeration and handlers in exhaustive visitors. There is no virtual
base visitor or numeric kind switch.

## Structural traversal

`for_each_child` enumerates immediate identity-bearing children in source
order. It does not recursively visit grandchildren. Each payload has an
explicit enumeration; there is no fallback that could hide a new node kind.
Tokens such as identifiers and lifetime names remain syntax fields, rather
than independent identity-bearing nodes.

Traversal visitors implement `enter(owner, payload) -> WalkControl` and
`leave(owner, payload) -> void`. `TraversalVisitor` checks coverage of the
whole tree. Inherit `DefaultVisitor` and use `using DefaultVisitor::enter`
and/or `using DefaultVisitor::leave` to explicitly choose selective handling.

- Continue traverses children, then calls leave.
- SkipChildren calls leave without visiting descendants.
- Stop prevents further enter calls, but balances leave for the current
  node and every entered ancestor, then returns WalkResult::Stopped.

Balancing applies to normal returns and controlled stops, not exceptions.
Each free traversal starts fresh; a stopped walker instance stays stopped.

## Selective traversal example

```cpp
#include "rx/ast/visitor.hpp"

struct CountBinaries : rx::ast::DefaultVisitor {
  using DefaultVisitor::enter;
  int count{};
  rx::ast::WalkControl enter(const rx::ast::Expr &node,
                            const rx::ast::BinaryExpr &binary) {
    ++count;
    // node.id/span identify this expression; binary.op/left/right describe it.
    return rx::ast::WalkControl::Continue;
  }
};
// CountBinaries counter;
// auto result = rx::ast::walk(crate, counter);
```

No nested visit or downcast is needed inside a traversal callback. For a
strict visitor, implement every enter and leave signature without inheriting
DefaultVisitor. `static_assert(TraversalVisitor<MyVisitor>)` checks coverage.

For simple preorder work, use `walk(crate, [](const Node& node, const auto& p)
{ /* inspect metadata and concrete payload */ });`. Each callback branch may
return void or WalkControl. Other return types are rejected. When an object
provides both a full hook interface and an operator(), its hooks take priority.

`for_each_child(root, callback)` passes child owners only; callers control
recursion and may call visit on a child when they need its payload. Item,
ImplMember and WherePredicate are also accepted as traversal roots.

## Migration

The old one-argument payload visit and one-argument Node walk callbacks have
been removed. Existing consumers must add the owner or payload parameter.
Semantic analysis still controls its own recursion because scopes and
expected types require context. Printing uses the same contextual dispatch.

The visitor regression passed with exact child order, one BinaryExpr callback,
balanced enter/leave stacks, subtree pruning, stopping before a sibling,
stopping at the root, and compile-time rejection of invalid traversal hooks.

## Initial verification

The `ast_visitor` CTest passed after adding compile-time coverage checks for
all union families, rejected handler signatures, and runtime owner/payload
identity checks. Reconfigure CMake after introducing the new test target:

```
cmake -S . -B build
cmake --build build --target ast_visitor -j 4
ctest --test-dir build -R "^ast_visitor$" --output-on-failure
```
