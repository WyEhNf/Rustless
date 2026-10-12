#pragma once

#include "rx/ast/children.hpp"

namespace rx::ast {
enum class WalkControl { Continue, SkipChildren, Stop };
enum class WalkResult { Completed, Stopped };

struct DefaultVisitor {
  template <class Payload> WalkControl enter(const Node &, const Payload &) {
    return WalkControl::Continue;
  }
  template <class Payload> void leave(const Node &, const Payload &) {}
};

namespace detail {
template <class Visitor, class Owner, class Payload, class Enable = void>
struct EnterCall : std::false_type {};

template <class Visitor, class Owner, class Payload>
struct EnterCall<Visitor, Owner, Payload,
                 std::void_t<decltype(std::declval<Visitor &>().enter(
                     std::declval<const Owner &>(),
                     std::declval<const Payload &>()))>> {
  using Result = decltype(std::declval<Visitor &>().enter(
      std::declval<const Owner &>(), std::declval<const Payload &>()));
  static constexpr bool value = std::is_same_v<Result, WalkControl>;
};

template <class Visitor, class Owner, class Payload, class Enable = void>
struct LeaveCall : std::false_type {};

template <class Visitor, class Owner, class Payload>
struct LeaveCall<Visitor, Owner, Payload,
                 std::void_t<decltype(std::declval<Visitor &>().leave(
                     std::declval<const Owner &>(),
                     std::declval<const Payload &>()))>> {
  using Result = decltype(std::declval<Visitor &>().leave(
      std::declval<const Owner &>(), std::declval<const Payload &>()));
  static constexpr bool value = std::is_same_v<Result, void>;
};

template <class Visitor> struct EnterAdapter {
  Visitor &visitor;

  template <class Owner, class Payload,
            std::enable_if_t<EnterCall<Visitor, Owner, Payload>::value, int> = 0>
  WalkControl operator()(const Owner &owner, const Payload &payload) {
    return visitor.enter(owner, payload);
  }
};
template <class Visitor> struct LeaveAdapter {
  Visitor &visitor;

  template <class Owner, class Payload,
            std::enable_if_t<LeaveCall<Visitor, Owner, Payload>::value, int> = 0>
  void operator()(const Owner &owner, const Payload &payload) {
    visitor.leave(owner, payload);
  }
};
template <class Visitor>
inline constexpr bool covers_tree = SyntaxOwners::covered<Visitor>;
}

template <class Visitor>
inline constexpr bool TraversalVisitor =
    detail::covers_tree<detail::EnterAdapter<Visitor>> &&
    detail::covers_tree<detail::LeaveAdapter<Visitor>>;

template <class Visitor> class Walker {
  static_assert(TraversalVisitor<Visitor>,
                "A tree visitor needs enter and leave handlers for all nodes");

public:
  explicit Walker(Visitor &visitor) : visitor_(visitor) {}

  template <class Owner, std::enable_if_t<SyntaxRoot<Owner>, int> = 0>
  WalkResult walk(const Owner &node) {
    if (stopped_)
      return WalkResult::Stopped;
    const WalkControl control =
        visit(node, detail::EnterAdapter<Visitor>{visitor_});
    if (control == WalkControl::Stop) {
      stopped_ = true;
    } else if (control == WalkControl::Continue) {
      for_each_child(node, [&](const auto &child) {
        walk(child);
      });
    }
    visit(node, detail::LeaveAdapter<Visitor>{visitor_});
    if (stopped_) {
      return WalkResult::Stopped;
    }
    return WalkResult::Completed;
  }

private:
  Visitor &visitor_;
  bool stopped_{};
};

template <class Owner, class Visitor,
          std::enable_if_t<SyntaxRoot<Owner> &&
                               TraversalVisitor<std::remove_reference_t<Visitor>>,
                           int> = 0>
WalkResult walk(const Owner &root, Visitor &&visitor) {
  Walker<std::remove_reference_t<Visitor>> walker(visitor);
  return walker.walk(root);
}

namespace detail {
template <class Callback, class Owner, class Payload, class Enable = void>
struct PreorderCall : std::false_type {};

template <class Callback, class Owner, class Payload>
struct PreorderCall<Callback, Owner, Payload,
                    std::void_t<std::invoke_result_t<Callback &, const Owner &,
                                                    const Payload &>>> {
  using Result =
      std::invoke_result_t<Callback &, const Owner &, const Payload &>;
  static constexpr bool value = std::is_same_v<Result, void> ||
                                std::is_same_v<Result, WalkControl>;
};

template <class Callback> struct PreorderAdapter : DefaultVisitor {
  Callback &callback;
  explicit PreorderAdapter(Callback &value) : callback(value) {}

  template <class Owner, class Payload,
            std::enable_if_t<PreorderCall<Callback, Owner, Payload>::value,
                             int> = 0>
  WalkControl enter(const Owner &owner, const Payload &payload) {
    using Result = typename PreorderCall<Callback, Owner, Payload>::Result;
    if constexpr (std::is_same_v<Result, void>) {
      std::invoke(callback, owner, payload);
      return WalkControl::Continue;
    } else {
      return std::invoke(callback, owner, payload);
    }
  }
};
}

template <class Callback>
inline constexpr bool TraversalCallback =
    TraversalVisitor<detail::PreorderAdapter<Callback>>;

template <class Owner, class Callback,
          std::enable_if_t<
              SyntaxRoot<Owner> &&
                  !TraversalVisitor<std::remove_reference_t<Callback>> &&
                  TraversalCallback<std::remove_reference_t<Callback>>,
              int> = 0>
WalkResult walk(const Owner &root, Callback &&callback) {
  detail::PreorderAdapter<std::remove_reference_t<Callback>> visitor(callback);
  return walk(root, visitor);
}
}
