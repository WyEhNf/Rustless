#include "rx/frontend.hpp"
#include "rx/semantic.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <string_view>

int main(int argc, char **argv) {
  const bool semantic = argc == 3 && std::string_view(argv[2]) == "--semantic";
  if (argc != 2 && !semantic) {
    std::cerr << "usage: frontend_ast_corpus <directory> [--semantic]\n";
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
      bool passed = parsed.syntax_ok && parsed.crate && !diagnostics.has_errors();
      if (passed && semantic) {
        rx::analyze(*parsed.crate, diagnostics);
        const auto name = entry.path().filename().string();
        if (name.starts_with("acc-"))
          passed = !diagnostics.has_errors();
        else if (name.starts_with("rej-"))
          passed = diagnostics.has_errors();
        else
          passed = false;
      }
      if (!passed) {
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
