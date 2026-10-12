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
}
