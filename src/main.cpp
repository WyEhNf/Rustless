#include "rx/ast/ast.hpp"
#include "rx/ast/frontend.hpp"
#include "rx/semantic/semantic.hpp"
#include "rx/llvm/lower.hpp"

#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>

namespace {

void usage(std::ostream &out) {
  out << "usage: rxc [--stage lex|parse|semantic|ir] [--parse-entry RULE]\n"
         "           [--dump-ast] [--dump-typed] [-o OUTPUT] SOURCE.rx\n"
         "parser entries: crate, item, expression, letStatement, typeRef\n";
}

int run(int argc, char **argv) {
  std::string stage = "semantic";
  std::string entry = "crate";
  std::string source;
  std::string output;
  bool dump_ast = false;
  bool dump_typed = false;
  for (int argument_index = 1; argument_index < argc; ++argument_index) {
    const std::string argument = argv[argument_index];
    auto next_argument = [&]() -> std::string {
      ++argument_index;
      if (argument_index == argc)
        throw std::runtime_error("missing value for " + argument);
      return argv[argument_index];
    };
    if (argument == "--help" || argument == "-h") {
      usage(std::cout);
      return 0;
    } else if (argument == "--stage")
      stage = next_argument();
    else if (argument == "--parse-entry")
      entry = next_argument();
    else if (argument == "--dump-ast")
      dump_ast = true;
    else if (argument == "--dump-typed")
      dump_typed = true;
    else if (argument == "-o")
      output = next_argument();
    else if (!argument.empty() && argument.front() == '-')
      throw std::runtime_error("unknown option '" + argument + "'");
    else if (source.empty())
      source = argument;
    else
      throw std::runtime_error("more than one source file was supplied");
  }
  const bool valid_entry = entry == "crate" || entry == "item" || entry == "expression" ||
                           entry == "letStatement" || entry == "typeRef";
  const bool valid_stage = stage == "lex" || stage == "parse" || stage == "semantic" || stage == "ir";
  if (source.empty() || !valid_stage || !valid_entry)
    throw std::runtime_error("invalid stage or option combination; use --help");
  if (entry != "crate" && stage != "parse")
    throw std::runtime_error("invalid stage or option combination; use --help");
  if ((dump_ast && (stage == "lex" || entry != "crate")) || (dump_typed && stage != "semantic") ||
      (stage == "ir" && (dump_ast || dump_typed)) ||
      (!output.empty() && !dump_ast && !dump_typed && stage != "ir"))
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
    if (accepted)
      return 0;
    return 1;
  }
  if (stage == "parse" && entry != "crate") {
    const bool accepted = rx::parse_syntax(buffer.str(), entry, diagnostics);
    diagnostics.print(std::cerr);
    if (accepted)
      return 0;
    return 1;
  }

  const auto frontend = rx::parse_source(buffer.str(), diagnostics);
  rx::SemanticResult semantic;
  if (frontend.syntax_ok && frontend.crate && (stage == "semantic" || stage == "ir"))
    semantic = rx::analyze(*frontend.crate, diagnostics);
  diagnostics.print(std::cerr);
  if (diagnostics.has_errors() || !frontend.syntax_ok || !frontend.crate)
    return 1;

  std::string artifact;
  if (dump_ast)
    artifact = rx::ast::dump(*frontend.crate);
  if (dump_typed)
    artifact += rx::dump_semantic(semantic);
  if (stage == "ir")
    artifact = rx::ir::print(rx::ir::lower(*frontend.crate, semantic));
  if (output.empty())
    std::cout << artifact;
  else {
    std::ofstream file(output, std::ios::binary);
    if (!file || !(file << artifact))
      throw std::runtime_error("cannot write '" + output + "'");
  }
  return 0;
}

}

int main(int argc, char **argv) {
  try {
    return run(argc, argv);
  } catch (const std::exception &error) {
    std::cerr << "error: " << error.what() << '\n';
    return 2;
  }
}
