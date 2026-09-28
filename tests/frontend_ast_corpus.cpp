#include "rx/frontend.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

int main(int argc, char **argv) {
  if (argc != 2) {
    std::cerr << "usage: frontend_ast_corpus <directory>\n";
    return 2;
  }
  std::size_t checked = 0;
  std::size_t failed = 0;
  for (const auto &entry : std::filesystem::recursive_directory_iterator(argv[1])) {
    if (!entry.is_regular_file() || entry.path().extension() != ".rx")
      continue;
    std::ifstream input(entry.path(), std::ios::binary);
    const std::string source{std::istreambuf_iterator<char>{input},
                             std::istreambuf_iterator<char>{}};
    rx::DiagnosticEngine diagnostics(entry.path().string());
    try {
      const auto parsed = rx::parse_source(source, diagnostics);
      if (!parsed.syntax_ok || !parsed.crate || diagnostics.has_errors()) {
        ++failed;
        std::cerr << entry.path() << '\n';
        diagnostics.print(std::cerr);
      }
    } catch (const std::exception &error) {
      ++failed;
      std::cerr << entry.path() << ": " << error.what() << '\n';
    }
    ++checked;
  }
  std::cout << "checked " << checked << " files, failed " << failed << '\n';
  return failed == 0 ? 0 : 1;
}
