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

void composite_shapes() {
  rx::TypeContext types, other;
  const auto integer = types.primitive(K::I32);
  const auto box = types.box(integer);
  const auto vector = types.vector(integer);
  const auto array = types.array(integer, 3);
  const auto reference = types.reference(integer, ast::Mutability::Immutable);
  const auto mutable_ref = types.reference(integer, ast::Mutability::Mutable);
  assert(box->kind() == K::Box && box->element() == integer);
  assert(vector->kind() == K::Vec && vector->element() == integer);
  assert(array->kind() == K::Array && array->length() == 3);
  assert(reference->kind() == K::Ref && !reference->is_mutable());
  assert(mutable_ref->is_mutable() && mutable_ref != reference);
  assert(rx::type_name(box) == "Box<i32>");
  assert(rx::type_name(vector) == "Vec<i32>");
  assert(rx::type_name(array) == "[i32; 3]");
  assert(rx::type_name(reference) == "&i32");
  assert(rx::type_name(mutable_ref) == "&mut i32");
  assert(box == types.box(integer) && vector == types.vector(integer));
  assert(array == types.array(integer, 3));
  assert(reference == types.reference(integer, ast::Mutability::Immutable));
  assert(!rx::same_type(array, types.array(integer, 4)));
  assert(!rx::same_type(box, vector));
  assert(!rx::same_type(reference, mutable_ref));
  assert(!rx::same_type(box, types.box(types.primitive(K::Error))));
  rejects<std::invalid_argument>([&] { types.box({}); });
  rejects<std::invalid_argument>([&] { types.vector({}); });
  rejects<std::invalid_argument>([&] { types.array({}, 0); });
  rejects<std::invalid_argument>(
      [&] { types.reference({}, ast::Mutability::Immutable); });
  rejects<std::invalid_argument>(
      [&] { types.reference(integer, static_cast<ast::Mutability>(999)); });
  rejects<std::logic_error>([&] { vector->length(); });
  rejects<std::logic_error>([&] { array->is_mutable(); });
  const auto foreign = other.array(other.vector(other.primitive(K::I32)), 3);
  const auto local = types.array(vector, 3);
  assert(foreign != local && rx::same_type(foreign, local));
  assert(types.box(foreign) == types.box(local));
  assert(types.box(foreign)->element() == local);
  rx::SemanticTypePtr survivor;
  {
    rx::TypeContext temporary;
    survivor = temporary.box(temporary.primitive(K::U32));
  }
  assert(rx::type_name(survivor) == "Box<u32>");
  assert(types.box(survivor)->element() == types.box(types.primitive(K::U32)));
}

int main() {
  atomic_shapes();
  composite_shapes();
}
