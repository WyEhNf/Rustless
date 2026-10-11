# Typed syntax tree

The AST represents syntax accepted by grammar/Parser.g4. ANTLR contexts
are consumed during construction and never retained by syntax nodes.

## Identity and ownership

- Nodes have a strong NodeId and a source span.
- A compilation unit owns its tree and source text.
- Required recursive children use a move-only Box with no empty constructor.
- Optional syntax uses optional<Box<T>>.
- Semantic facts are indexed by syntax identity, outside the tree.
- Symbol identity is distinct from syntax identity.

## Closed node families

Expr, TypeSyntax and Stmt contain a variant of dedicated payloads.
The variant is the sole node discriminator. There are no parallel kind
tags or universal operand/text/flag fields.

Binary, unary and compound assignment operators have separate enums.
Borrowing and ordinary assignment have their own payloads.
Required operands have names and fixed cardinality.

## Types and names

Types cover unit, paths, references, arrays and parentheses.
Array lengths are constant expressions rather than source strings.
References retain optional lifetime syntax and mutability.
Path segments own their generic arguments; lifetime arguments are retained.
Identifiers and lifetimes carry their own source positions.

## Statements and blocks

Statements cover empty, let and expression statements.
Blocks own an ordered statement list and an optional value tail.
Expression statements retain whether a semicolon was present.
The final unterminated expression follows the language's block-tail rules.

## Declarations

Crate owns ordered use, function, struct, constant and impl declarations.
Impl owns ordered function/constant members.
Function signatures distinguish value and reference self parameters.
Missing return annotations stay optional until semantic resolution.
Lifetime parameter bounds and where predicates are preserved.
Derive names are enums with individual source locations.
Use trees preserve prefixes, aliases, wildcard imports and nested groups.

## Consumers

Printing and semantic dispatch use exhaustive variant visitors.
A separate structural walker visits children in source order.
Parentheses are preserved in syntax and transparent to type checking.
Lowering consumes checked semantic facts without mutating syntax.
The current IR infrastructure has no AST lowering implementation to migrate.

## Verification

Run the existing CTest suite and external frontend manifests.
Add structural checks for each node family, path arguments, use trees,
constraints, block tails, source spans and unique node identities.
Compile-time checks cover ownership and distinct operator/identity types.
Rejected syntax produces no AST. Dump output remains deterministic.

## Migration

Introduce the models, port construction, port printing and semantic
consumers, then validate and remove all legacy representations.
Commit each logical migration slice; never commit user edits as part
of an unrelated migration slice.
