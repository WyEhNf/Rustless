#include "rx/ast/visitor.hpp"
#include "rx/frontend.hpp"
#include "rx/semantic.hpp"
#include "rx/semantic_index.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>

int main(int argc, char **argv) {
  const bool check_semantics = argc == 3 && std::string_view(argv[2]) == "--semantic";
  if (argc != 2 && !check_semantics) {
    std::cerr << "usage: frontend_ast_corpus <directory> [--semantic]\n";
    return 2;
  }
  std::size_t checked_files = 0;
  std::size_t failed_files = 0;
  for (const auto &entry : std::filesystem::recursive_directory_iterator(argv[1])) {
    if (!entry.is_regular_file() || entry.path().extension() != ".rx")
      continue;
    std::ifstream input(entry.path(), std::ios::binary);
    const std::string source{std::istreambuf_iterator<char>{input},
                             std::istreambuf_iterator<char>{}};
    rx::DiagnosticEngine diagnostics(entry.path().string());
    try {
      const auto parsed = rx::parse_source(source, diagnostics);
      bool passed = parsed.syntax_ok && parsed.crate && !diagnostics.has_errors();
      if (passed) {
        std::set<rx::ast::NodeId> identities;
        rx::ast::walk(*parsed.crate, [&](const rx::ast::Node &node, const auto &) {
          if (node.id.value == 0 || !identities.insert(node.id).second)
            throw std::logic_error("duplicate or missing syntax identity");
          if (node.span.begin > node.span.end || node.span.end > source.size() ||
              node.span.file != parsed.crate->source.id)
            throw std::logic_error("invalid syntax source span");
        });
        const auto dump = rx::ast::dump(*parsed.crate);
        if (dump.compare(0, 5, "Crate") != 0)
          throw std::logic_error("missing AST dump root");
      }
      if (passed && check_semantics) {
        const auto semantic_result = rx::analyze(*parsed.crate, diagnostics);
        if (!diagnostics.has_errors()) {
          rx::SemanticIndex index(*parsed.crate, semantic_result);
          (void)index;
        }
        const auto name = entry.path().filename().string();
        if (name.compare(0, 4, "acc-") == 0)
          passed = !diagnostics.has_errors();
        else if (name.compare(0, 4, "rej-") == 0)
          passed = diagnostics.has_errors();
        else
          passed = false;
      }
      if (!passed) {
        ++failed_files;
        std::cerr << entry.path() << '\n';
        diagnostics.print(std::cerr);
      }
    } catch (const std::exception &error) {
      ++failed_files;
      std::cerr << entry.path() << ": " << error.what() << '\n';
    }
    ++checked_files;
  }
  std::cout << "checked " << checked_files << " files, failed " << failed_files << '\n';
  if (failed_files == 0)
    return 0;
  return 1;
}
