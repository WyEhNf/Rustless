#pragma once

#include "rx/ast/children.hpp"

namespace rx::ast {
enum class WalkControl { Continue, SkipChildren, Stop };
enum class WalkResult { Completed, Stopped };

// Inherit explicitly for a selective visitor; strict visitors have no fallback.
struct DefaultVisitor {
  template <class P> WalkControl enter(const Node &, const P &) {
    return WalkControl::Continue;
  }
  template <class P> void leave(const Node &, const P &) {}
};

namespace detail {
template <class V> struct EnterAdapter {
  V &visitor;
  template <class O, class P>
    requires requires(V &v, const O &owner, const P &p) {
      { v.enter(owner, p) } -> std::same_as<WalkControl>;
    }
  WalkControl operator()(const O &owner, const P &p) {
    return visitor.enter(owner, p);
  }
};
template <class V> struct LeaveAdapter {
  V &visitor;
  template <class O, class P>
    requires requires(V &v, const O &owner, const P &p) {
      { v.leave(owner, p) } -> std::same_as<void>;
    }
  void operator()(const O &owner, const P &p) { visitor.leave(owner, p); }
};
template <class V> constexpr bool covers_tree =
    ExhaustiveVisitor<V, Crate> && ExhaustiveVisitor<V, Expr> &&
    ExhaustiveVisitor<V, TypeSyntax> && ExhaustiveVisitor<V, Stmt> &&
    ExhaustiveVisitor<V, Block> && ExhaustiveVisitor<V, Item> &&
    ExhaustiveVisitor<V, UseTree> && ExhaustiveVisitor<V, IfBranch> &&
    ExhaustiveVisitor<V, SelfParam> && ExhaustiveVisitor<V, Parameter> &&
    ExhaustiveVisitor<V, StructField> && ExhaustiveVisitor<V, FieldInit> &&
    ExhaustiveVisitor<V, LifetimeParameter> && ExhaustiveVisitor<V, WherePredicate>;
} // namespace detail

template <class V>
concept TraversalVisitor = detail::covers_tree<detail::EnterAdapter<V>> &&
                           detail::covers_tree<detail::LeaveAdapter<V>>;

template <TraversalVisitor V> class TypedWalker {
public:
  explicit TypedWalker(V &visitor) : visitor_(visitor) {}
  template <class T> WalkResult walk(const T &node) {
    if (stopped_)
      return WalkResult::Stopped;
    auto control = dispatch(node, detail::EnterAdapter<V>{visitor_});
    if (control == WalkControl::Stop)
      stopped_ = true;
    else if (control == WalkControl::Continue)
      for_each_child(node, [&](const auto &child) { walk(child); });
    // Balance every successful enter, including skip and stop, on unwinding.
    dispatch(node, detail::LeaveAdapter<V>{visitor_});
    return stopped_ ? WalkResult::Stopped : WalkResult::Completed;
  }

private:
  V &visitor_;
  bool stopped_{};
};

template <class T, class V> requires TraversalVisitor<std::remove_reference_t<V>>
WalkResult walk_typed(const T &root, V &&visitor) {
  TypedWalker<std::remove_reference_t<V>> walker(visitor);
  return walker.walk(root);
}
} // namespace rx::ast
