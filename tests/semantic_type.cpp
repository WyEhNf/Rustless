#include "rx/semantic_type.hpp"

#undef NDEBUG
#include <array>
#include <cassert>
#include <stdexcept>
#include <type_traits>

using K = rx::SemanticType::Kind;
namespace ast = rx::ast;

struct KindObserver {
  template <class P> K operator()(const rx::SemanticType &owner, const P &) {
    return owner.kind();
  }
};
struct OnlyArray {
  K operator()(const rx::SemanticType &, const rx::ArrayType &);
};
struct MixedReturns : KindObserver {
  using KindObserver::operator();
  void operator()(const rx::SemanticType &, const rx::ArrayType &);
};
static_assert(std::is_const_v<rx::SemanticTypePtr::element_type>);
static_assert(!std::is_default_constructible_v<rx::SemanticType>);
static_assert(
    !std::is_constructible_v<rx::SemanticType, rx::SemanticType::Data>);
static_assert(
    std::same_as<decltype(std::declval<const rx::SemanticType &>().data()),
                 const rx::SemanticType::Data &>);
static_assert(std::same_as<decltype(rx::get_if<rx::ArrayType>(
                               std::declval<const rx::SemanticType &>())),
                           const rx::ArrayType *>);
static_assert(rx::SemanticTypeVisitor<KindObserver>);
static_assert(!rx::SemanticTypeVisitor<OnlyArray>);
static_assert(!rx::SemanticTypeVisitor<MixedReturns>);

template <class Exception, class Action> void rejects(Action action) {
  bool rejected = false;
  try {
    action();
  } catch (const Exception &) {
    rejected = true;
  }
  assert(rejected);
}

void atomic_shapes() {
  rx::TypeContext types;
  constexpr std::array kinds{K::Error, K::Never, K::Unit,  K::Bool,
                             K::I32,   K::U32,   K::Isize, K::Usize};
  constexpr std::array names{"<error>", "!",   "()",    "bool",
                             "i32",     "u32", "isize", "usize"};
  for (std::size_t i = 0; i < kinds.size(); ++i) {
    const auto value = types.primitive(kinds[i]);
    assert(value->kind() == kinds[i]);
    assert(value == types.primitive(kinds[i]));
    assert(rx::type_name(value) == names[i]);
    assert(rx::visit(*value, KindObserver{}) == kinds[i]);
    rejects<std::logic_error>([&] { value->element(); });
    rejects<std::logic_error>([&] { value->length(); });
    rejects<std::logic_error>([&] { value->is_mutable(); });
    rejects<std::logic_error>([&] { value->name(); });
    rejects<std::logic_error>([&] { value->symbol(); });
  }
  rejects<std::invalid_argument>([&] { types.primitive(K::Array); });
  rejects<std::invalid_argument>([&] { types.primitive(static_cast<K>(999)); });
  const auto error = types.primitive(K::Error);
  assert(!rx::same_type(error, types.primitive(K::I32)));
  assert(!rx::same_type({}, {}));
  assert(rx::type_name({}) == "<missing>");
}

int main() { atomic_shapes(); }
