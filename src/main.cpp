#include "rx/ast.hpp"
#include "rx/frontend.hpp"
#include "rx/semantic.hpp"

#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>

namespace {

void usage(std::ostream &out) {
  out << "usage: rxc [--stage lex|parse|semantic] [--parse-entry RULE]\n"
         "           [--dump-ast] [--dump-typed] [-o OUTPUT] SOURCE.rx\n"
         "parser entries: crate, item, expression, letStatement, typeRef\n";
}

int run(int argc, char **argv) {
  std::string stage = "semantic", entry = "crate", source, output;
  bool dump_ast = false, dump_typed = false;
  for (int i = 1; i < argc; ++i) {
    const std::string argument = argv[i];
    auto next = [&]() -> std::string {
      if (++i == argc)
        throw std::runtime_error("missing value for " + argument);
      return argv[i];
    };
    if (argument == "--help" || argument == "-h") {
      usage(std::cout);
      return 0;
    } else if (argument == "--stage")
      stage = next();
    else if (argument == "--parse-entry")
      entry = next();
    else if (argument == "--dump-ast")
      dump_ast = true;
    else if (argument == "--dump-typed")
      dump_typed = true;
    else if (argument == "-o")
      output = next();
    else if (argument.starts_with('-'))
      throw std::runtime_error("unknown option '" + argument + "'");
    else if (source.empty())
      source = argument;
    else
      throw std::runtime_error("more than one source file was supplied");
  }
  const bool valid_entry = entry == "crate" || entry == "item" ||
                           entry == "expression" || entry == "letStatement" ||
                           entry == "typeRef";
  if (source.empty() ||
      (stage != "lex" && stage != "parse" && stage != "semantic") ||
      !valid_entry || (entry != "crate" && stage != "parse") ||
      (dump_ast && (stage == "lex" || entry != "crate")) ||
      (dump_typed && stage != "semantic") ||
      (!output.empty() && !dump_ast && !dump_typed))
    throw std::runtime_error("invalid stage or option combination; use --help");

  std::ifstream input(source, std::ios::binary);
  if (!input)
    throw std::runtime_error("cannot open '" + source + "'");
  std::ostringstream buffer;
  buffer << input.rdbuf();
  if (input.bad())
    throw std::runtime_error("cannot read '" + source + "'");
  rx::DiagnosticEngine diagnostics(source);
  if (stage == "lex") {
    const bool accepted = rx::lex_source(buffer.str(), diagnostics);
    diagnostics.print(std::cerr);
    return accepted ? 0 : 1;
  }
  if (stage == "parse" && entry != "crate") {
    const bool accepted = rx::parse_syntax(buffer.str(), entry, diagnostics);
    diagnostics.print(std::cerr);
    return accepted ? 0 : 1;
  }

  const auto frontend = rx::parse_source(buffer.str(), diagnostics);
  rx::SemanticResult semantic;
  if (frontend.syntax_ok && frontend.crate && stage == "semantic")
    semantic = rx::analyze(*frontend.crate, diagnostics);
  diagnostics.print(std::cerr);
  // Rejected source never produces an AST or semantic output file.
  if (diagnostics.has_errors() || !frontend.syntax_ok || !frontend.crate)
    return 1;

  std::string artifact;
  if (dump_ast)
    artifact = rx::ast::dump(*frontend.crate);
  if (dump_typed)
    artifact += rx::dump_semantic(semantic);
  if (output.empty())
    std::cout << artifact;
  else {
    std::ofstream file(output, std::ios::binary);
    if (!file || !(file << artifact))
      throw std::runtime_error("cannot write '" + output + "'");
  }
  return 0;
}

} // namespace

int main(int argc, char **argv) {
  // Exit 1 denotes source rejection; exit 2 denotes invocation/internal errors.
  try {
    return run(argc, argv);
  } catch (const std::exception &error) {
    std::cerr << "error: " << error.what() << '\n';
    return 2;
  }
}
