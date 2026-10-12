#include "rx/frontend.hpp"
#include "rx/semantic_index.hpp"

#undef NDEBUG
#include <algorithm>
#include <cassert>
#include <memory>
#include <stdexcept>

int main() {
  rx::DiagnosticEngine diagnostics;
  auto parsed = rx::parse_source(R"(
const N: usize = 2;
struct Pair { enabled: bool, value: i32 }
impl Pair { fn get(&self) -> i32 { self.value } }
fn choose(value: i32, enabled: bool) -> i32 {
    if enabled { value } else { 0 }
}
fn main() {
    let p = Pair { enabled: true, value: 7 };
    let values: [i32; N] = [p.get(), choose(2, false)];
    printInt(values[0]);
    let mut count = 1;
    count += 2;
    let sum = &count + &count;
    printInt(sum);
    let a = Box::<i32>::new(1);
    let b = Box::<i32>::new(1);
    let equal = a == b;
}
)", diagnostics);
  assert(parsed.syntax_ok && parsed.crate);
  auto result = rx::analyze(*parsed.crate, diagnostics);
  assert(result.success() && !diagnostics.has_errors());
  const auto original = result.data();
  auto rejects = [&](auto mutate) {
    auto draft = original;
    mutate(draft);
    bool rejected = false;
    try {
      (void)rx::SemanticResult::publish(*parsed.crate, std::move(draft));
    } catch (const std::exception &) {
      rejected = true;
    }
    assert(rejected);
  };
  auto named = [&](std::string_view name) {
    const auto found = std::find_if(original.symbols.begin(), original.symbols.end(),
        [&](const auto &record) { return record.name == name; });
    assert(found != original.symbols.end());
    return found->id;
  };
  const auto choose = named("choose");
  const auto get = named("Pair::get");
  rejects([](auto &draft) { draft.structures.front().fields.pop_back(); });
  rejects([](auto &draft) {
    auto &fields = draft.structures.front().fields;
    std::swap(fields[0], fields[1]);
  });
  rejects([&](auto &draft) {
    for (auto &function : draft.functions)
      if (function.symbol == choose)
        function.parameters.clear();
  });
  rejects([&](auto &draft) {
    for (auto &function : draft.functions)
      if (function.symbol == choose)
        std::swap(function.parameters[0], function.parameters[1]);
  });
  rejects([&](auto &draft) {
    for (auto &function : draft.functions)
      if (function.symbol == get)
        function.receiver.reset();
  });
  rejects([](auto &draft) { draft.expressions.pop_back(); });
  rejects([](auto &draft) { draft.resolved_types.pop_back(); });
  rejects([](auto &draft) { draft.constants.clear(); });
  rejects([](auto &draft) { draft.expressions.push_back(draft.expressions.front()); });
  rejects([](auto &draft) { draft.symbol_types.push_back(draft.symbol_types.front()); });
  const auto value_field = named("value");
  rejects([&](auto &draft) {
    rx::TypeContext types;
    for (auto &fact : draft.symbol_types)
      if (fact.symbol == value_field)
        fact.type = types.primitive(rx::SemanticType::Kind::Bool);
  });
  rejects([&](auto &draft) {
    for (auto &record : draft.symbols)
      if (record.id == value_field)
        record.owner = choose;
  });
  rejects([&](auto &draft) {
    rx::TypeContext types;
    for (auto &fact : draft.functions)
      if (fact.symbol == choose)
        fact.result = types.primitive(rx::SemanticType::Kind::Unit);
  });
  rejects([](auto &draft) {
    rx::TypeContext types;
    draft.expressions.front().type =
        types.box(types.primitive(rx::SemanticType::Kind::Error));
  });
  rejects([&](auto &draft) {
    rx::TypeContext types;
    draft.expressions.front().type = types.structure(choose, "choose");
  });
  rejects([](auto &draft) {
    rx::TypeContext types;
    draft.expressions.front().type =
        types.structure(rx::SymbolId{999999}, "Missing");
  });
  rejects([](auto &draft) {
    draft.expressions.front().node = draft.symbols.front().declaration;
  });
  auto binary_fact = [](auto &draft) -> auto & {
    const auto found = std::find_if(draft.expressions.begin(), draft.expressions.end(),
        [](const auto &fact) { return fact.binary.has_value(); });
    assert(found != draft.expressions.end());
    return *found;
  };
  rejects([&](auto &draft) { binary_fact(draft).binary.reset(); });
  rejects([&](auto &draft) {
    auto &operation = *binary_fact(draft).binary;
    std::swap(operation.left.node, operation.right.node);
  });
  rejects([&](auto &draft) {
    rx::TypeContext types;
    binary_fact(draft).binary->left.effective_type =
        types.primitive(rx::SemanticType::Kind::Bool);
  });
  rejects([&](auto &draft) {
    binary_fact(draft).binary->operation =
        rx::IntegerArithmetic{static_cast<rx::ArithmeticOp>(99)};
  });
  rejects([&](auto &draft) {
    for (auto &fact : draft.expressions)
      if (fact.binary &&
          std::holds_alternative<rx::StructuralEquality>(fact.binary->operation)) {
        fact.binary->operation = rx::ScalarComparison{rx::Relation::Eq};
        return;
      }
    assert(false);
  });
  rejects([](auto &draft) {
    for (auto &fact : draft.expressions)
      if (fact.compound) {
        fact.compound.reset();
        return;
      }
    assert(false);
  });
  rejects([&](auto &draft) {
    for (auto &fact : draft.expressions)
      if (fact.callable) {
        fact.callable = rx::FunctionTarget{value_field};
        return;
      }
    assert(false);
  });
  static_assert(std::is_const_v<rx::SemanticTypePtr::element_type>);
  std::unique_ptr<rx::SemanticIndex> retained;
  {
    auto temporary = rx::SemanticResult::publish(*parsed.crate, original);
    retained = std::make_unique<rx::SemanticIndex>(*parsed.crate, temporary);
  }
  assert(retained->function(choose).parameters.size() == 2);
  auto failed = rx::SemanticResult::rejected(*parsed.crate, original);
  assert(!failed.success());
  bool rejected_failed = false;
  try {
    rx::SemanticIndex invalid(*parsed.crate, failed);
  } catch (const std::exception &) {
    rejected_failed = true;
  }
  assert(rejected_failed);
}
