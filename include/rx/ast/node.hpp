#pragma once

#include "rx/diagnostic.hpp"
#include <compare>
#include <cstdint>
#include <functional>
#include <memory>
#include <ostream>
#include <stdexcept>
#include <utility>

namespace rx::ast {

template <class Tag> struct Id {
  std::uint32_t value{};
  auto operator<=>(const Id &) const = default;
};
struct NodeTag;
using NodeId = Id<NodeTag>;
template <class Tag> std::ostream &operator<<(std::ostream &out, Id<Tag> id) {
  return out << id.value;
}

struct Node {
  NodeId id{};
  Span span{};
};

// Required children have exclusive ownership and cannot be constructed empty.
template <class T> class Box {
public:
  explicit Box(std::unique_ptr<T> value)
      : value_(value.release(), [](T *pointer) { delete pointer; }) {
    if (!value_)
      throw std::invalid_argument("a required AST child cannot be null");
  }
  Box(Box &&) noexcept = default;
  Box &operator=(Box &&) noexcept = default;
  Box(const Box &) = delete;
  Box &operator=(const Box &) = delete;
  T &operator*() { return *value_; }
  const T &operator*() const { return *value_; }
  T *operator->() { return value_.get(); }
  const T *operator->() const { return value_.get(); }
  T *get() { return value_.get(); }
  const T *get() const { return value_.get(); }
private:
  // The deleter is created where T is complete; public recursive headers
  // can destroy a Box without including every other node family.
  std::unique_ptr<T, void (*)(T *)> value_;
};

template <class T, class... Args> Box<T> box(Args &&...args) {
  return Box<T>{std::make_unique<T>(std::forward<Args>(args)...)};
}

template <class... Fs> struct Overloaded : Fs... { using Fs::operator()...; };
template <class... Fs> Overloaded(Fs...) -> Overloaded<Fs...>;

} // namespace rx::ast

namespace std {
template <class Tag> struct hash<rx::ast::Id<Tag>> {
  size_t operator()(rx::ast::Id<Tag> id) const noexcept {
    return hash<std::uint32_t>{}(id.value);
  }
};
} // namespace std
