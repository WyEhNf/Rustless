#include "rx/ast/frontend.hpp"
#include "rx/llvm/lower.hpp"
#include "rx/llvm/verify.hpp"

#undef NDEBUG
#include <cassert>
#include <iostream>

namespace {

rx::ir::Module check(const std::string &source) {
  rx::DiagnosticEngine diagnostics;
  auto parsed = rx::parse_source(source, diagnostics);
  assert(parsed.syntax_ok);
  auto semantic = rx::analyze(*parsed.crate, diagnostics);
  diagnostics.print(std::cerr);
  assert(semantic.success());
  const auto module = rx::ir::lower(*parsed.crate, semantic);
  const auto errors = rx::ir::verify(module);
  for (const auto &error : errors)
    std::cerr << error.function << ": " << error.message << '\n';
  assert(errors.empty());
  for (const auto &function : module.functions) {
    assert(!function.blocks.empty());
    for (const auto &block : function.blocks) {
      assert(!block.instructions.empty());
      assert(rx::ir::is_terminator(block.instructions.back()));
      for (const auto &instruction : block.instructions)
        if (std::holds_alternative<rx::ir::Alloca>(instruction.data))
          assert(block.id == function.blocks.front().id);
    }
  }
  assert(rx::ir::print(module).find("define i32 @main()") != std::string::npos);
  return module;
}

}

int main() {
  check(R"(
fn choose(flag: bool) -> i32 {
  if flag { return 7; } else { return 9; }
}
fn main() {
  let mut value = 0;
  while value < 4 { value += 1; if value == 2 { continue; } }
  let result = loop { if value == 4 { break choose(true); } };
  printInt(result);
  let unused = false && { return; true };
}
)");
  check(R"(
#[derive(Clone, PartialEq, Eq)]
struct Tree { value: i32, children: Vec<Tree> }
impl Tree {
  fn get(&self) -> i32 { self.value }
}
fn identity(value: [Tree; 1]) -> [Tree; 1] { value }
fn main() {
  let mut root = Tree { children: Vec::<Tree>::new(), value: 3 };
  root.children.push(Tree { value: 4, children: Vec::<Tree>::new() });
  let copied = root.clone();
  let values = identity([copied]);
  let boxed = Box::<[Tree; 1]>::new(values);
  printInt(boxed[0].get());
  let same = root == boxed[0];
  let removed = root.children.remove(0);
}
)");
  check(R"(
fn diverges() -> i32 { { return 12; 0 }; 9 }
fn unit(value: ()) { value; }
fn main() {
  let mut x = 1;
  let reference = &mut x;
  let shared: &i32 = reference;
  let arrays: [&i32; 2] = [shared, &3];
  let empty: [(); 0] = [unit(()); 0];
  printlnInt(*arrays[0] + diverges());
}
)");
  const auto large = check(R"(
fn main() {
  let mut values = [0; 4096];
  values[4095] = 7;
  printInt(values[4095]);
}
)");
  for (const auto &function : large.functions) {
    if (!function.symbol)
      continue;
    std::uint32_t frame_bytes = 0;
    for (const auto &instruction : function.blocks.front().instructions)
      if (const auto *allocation = std::get_if<rx::ir::Alloca>(&instruction.data))
        frame_bytes += allocation->bytes;
    assert(frame_bytes < 20 * 1024);
  }
}
