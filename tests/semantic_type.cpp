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
struct MutablePayload {
  template <class P>
  void operator()(const rx::SemanticType &, P &)
    requires(!std::is_const_v<P>)
  {}
};
struct ReferenceObserver {
  template <class P>
  const rx::SemanticType &operator()(const rx::SemanticType &owner, const P &) {
    return owner;
  }
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
static_assert(!rx::SemanticTypeVisitor<MutablePayload>);
static_assert(
    std::same_as<decltype(rx::visit(std::declval<const rx::SemanticType &>(),
                                    std::declval<ReferenceObserver &>())),
                 const rx::SemanticType &>);

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
  assert(&rx::visit(*array, ReferenceObserver{}) == array.get());
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

void nominal_and_builtin_shapes() {
  rx::TypeContext types, other;
  const auto structure = types.structure(rx::SymbolId{1}, "Pair");
  const auto same_spelling = types.structure(rx::SymbolId{2}, "Pair");
  const auto function = types.function(rx::SymbolId{3}, "Pair::get");
  assert(structure == types.structure(rx::SymbolId{1}, "Pair"));
  assert(structure->symbol() == rx::SymbolId{1} && structure->name() == "Pair");
  assert(function->kind() == K::Function &&
         function->symbol() == rx::SymbolId{3});
  assert(rx::type_name(function) == "fn Pair::get");
  assert(!rx::same_type(structure, same_spelling));
  assert(!rx::same_type(structure, function));
  assert(rx::same_type(structure, other.structure(rx::SymbolId{1}, "Pair")));
  rejects<std::invalid_argument>([&] { types.structure({}, "Invalid"); });
  rejects<std::invalid_argument>([&] { types.function(rx::SymbolId{4}, ""); });
  rejects<std::invalid_argument>(
      [&] { types.structure(rx::SymbolId{1}, "Other"); });
  rejects<std::invalid_argument>(
      [&] { types.function(rx::SymbolId{1}, "Pair"); });
  rejects<std::logic_error>([&] { function->element(); });
  const auto io = types.builtin(rx::BuiltinId::PrintInt);
  const auto vector = types.vector(types.primitive(K::I32));
  const auto len = types.builtin(rx::BuiltinId::Len, vector);
  assert(io->kind() == K::BuiltinFunction);
  assert(io == types.builtin(rx::BuiltinId::PrintInt));
  assert(io != types.builtin(rx::BuiltinId::GetInt));
  assert(!rx::get_if<rx::BuiltinFunctionType>(*io)->owner);
  assert(rx::get_if<rx::BuiltinFunctionType>(*len)->owner == vector);
  assert(len == types.builtin(rx::BuiltinId::Len, vector));
  assert(!rx::same_type(len, types.builtin(rx::BuiltinId::Len)));
  assert(!rx::same_type(io, types.function(rx::SymbolId{5}, "printInt")));
  assert(rx::same_type(io, other.builtin(rx::BuiltinId::PrintInt)));
  const auto foreign_owner = other.vector(other.primitive(K::I32));
  assert(types.builtin(rx::BuiltinId::Len, foreign_owner) == len);
  assert(
      types.box(other.builtin(rx::BuiltinId::Len, foreign_owner))->element() ==
      len);
  rejects<std::logic_error>([&] { io->symbol(); });
  rejects<std::invalid_argument>(
      [&] { types.builtin(static_cast<rx::BuiltinId>(999)); });
}

int main() {
  atomic_shapes();
  composite_shapes();
  nominal_and_builtin_shapes();
}
