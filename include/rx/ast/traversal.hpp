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
  void operator()(const O &owner, const P &p) {
    visitor.leave(owner, p);
  }
};
template <class V> constexpr bool covers_tree = SyntaxOwners::covered<V>;
} // namespace detail

template <class V>
concept TraversalVisitor = detail::covers_tree<detail::EnterAdapter<V>> &&
                           detail::covers_tree<detail::LeaveAdapter<V>>;

template <TraversalVisitor V> class Walker {
public:
  explicit Walker(V &visitor) : visitor_(visitor) {}
  template <SyntaxRoot T> WalkResult walk(const T &node) {
    if (stopped_)
      return WalkResult::Stopped;
    auto control = visit(node, detail::EnterAdapter<V>{visitor_});
    if (control == WalkControl::Stop)
      stopped_ = true;
    else if (control == WalkControl::Continue)
      for_each_child(node, [&](const auto &child) { walk(child); });
    // Balance every successful enter, including skip and stop, on unwinding.
    visit(node, detail::LeaveAdapter<V>{visitor_});
    return stopped_ ? WalkResult::Stopped : WalkResult::Completed;
  }

private:
  V &visitor_;
  bool stopped_{};
};

template <SyntaxRoot T, class V>
  requires TraversalVisitor<std::remove_reference_t<V>>
WalkResult walk(const T &root, V &&visitor) {
  Walker<std::remove_reference_t<V>> walker(visitor);
  return walker.walk(root);
}

namespace detail {
template <class V> struct PreorderAdapter : DefaultVisitor {
  V &callback;
  explicit PreorderAdapter(V &value) : callback(value) {}
  // Hide DefaultVisitor::enter: missing callback handlers remain errors.
  template <class O, class P>
    requires std::invocable<V &, const O &, const P &> &&
             (std::same_as<std::invoke_result_t<V &, const O &, const P &>,
                           void> ||
              std::same_as<std::invoke_result_t<V &, const O &, const P &>,
                           WalkControl>)
  WalkControl enter(const O &owner, const P &p) {
    if constexpr (std::same_as<std::invoke_result_t<V &, const O &, const P &>,
                               void>) {
      std::invoke(callback, owner, p);
      return WalkControl::Continue;
    } else {
      return std::invoke(callback, owner, p);
    }
  }
};
} // namespace detail

template <class V>
concept TraversalCallback = TraversalVisitor<detail::PreorderAdapter<V>>;

template <SyntaxRoot T, class V>
  requires(!TraversalVisitor<std::remove_reference_t<V>>) &&
          TraversalCallback<std::remove_reference_t<V>>
WalkResult walk(const T &root, V &&callback) {
  detail::PreorderAdapter<std::remove_reference_t<V>> visitor(callback);
  return walk(root, visitor);
}
} // namespace rx::ast
