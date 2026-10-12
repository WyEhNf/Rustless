#pragma once

#include "rx/diagnostic.hpp"
#include <cstdint>
#include <functional>
#include <memory>
#include <ostream>
#include <stdexcept>
#include <utility>

namespace rx::ast {

template <class Tag> struct Id {
  std::uint32_t value{};

  bool operator==(const Id &other) const { return value == other.value; }
  bool operator!=(const Id &other) const { return value != other.value; }
  bool operator<(const Id &other) const { return value < other.value; }
  bool operator<=(const Id &other) const { return value <= other.value; }
  bool operator>(const Id &other) const { return value > other.value; }
  bool operator>=(const Id &other) const { return value >= other.value; }
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

template <class Value> class Box {
public:
  explicit Box(std::unique_ptr<Value> value)
      : value_(value.release(), [](Value *pointer) { delete pointer; }) {
    if (!value_)
      throw std::invalid_argument("a required AST child cannot be null");
  }
  Box(Box &&) noexcept = default;
  Box &operator=(Box &&) noexcept = default;
  Box(const Box &) = delete;
  Box &operator=(const Box &) = delete;
  Value &operator*() { return *value_; }
  const Value &operator*() const { return *value_; }
  Value *operator->() { return value_.get(); }
  const Value *operator->() const { return value_.get(); }
  Value *get() { return value_.get(); }
  const Value *get() const { return value_.get(); }

private:
  std::unique_ptr<Value, void (*)(Value *)> value_;
};

template <class Value, class... Arguments>
Box<Value> box(Arguments &&...arguments) {
  return Box<Value>{
      std::make_unique<Value>(std::forward<Arguments>(arguments)...)};
}

template <class... Functions> struct Overloaded : Functions... {
  using Functions::operator()...;
};
template <class... Functions>
Overloaded(Functions...) -> Overloaded<Functions...>;

}

namespace std {
template <class Tag> struct hash<rx::ast::Id<Tag>> {
  size_t operator()(rx::ast::Id<Tag> id) const noexcept {
    return hash<std::uint32_t>{}(id.value);
  }
};
}
