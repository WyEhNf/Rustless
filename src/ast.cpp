#include "rx/ast.hpp"

#include <sstream>

namespace rx {

DiagnosticEngine::DiagnosticEngine(std::string file) : file_(std::move(file)) {}

void DiagnosticEngine::error(Span span, std::string message) {
  diagnostics_.push_back({Severity::Error, span, std::move(message)});
}

void DiagnosticEngine::warning(Span span, std::string message) {
  diagnostics_.push_back({Severity::Warning, span, std::move(message)});
}

bool DiagnosticEngine::has_errors() const {
  for (const auto &diagnostic : diagnostics_) {
    if (diagnostic.severity == Severity::Error)
      return true;
  }
  return false;
}

std::size_t DiagnosticEngine::error_count() const {
  std::size_t count = 0;
  for (const auto &diagnostic : diagnostics_)
    count += diagnostic.severity == Severity::Error;
  return count;
}

const std::vector<Diagnostic> &DiagnosticEngine::diagnostics() const {
  return diagnostics_;
}

void DiagnosticEngine::print(std::ostream &out) const {
  // Keep diagnostics stable for both humans and the course harness.
  for (const auto &diagnostic : diagnostics_) {
    if (!file_.empty())
      out << file_ << ':';
    out << diagnostic.span.line << ':' << (diagnostic.span.column + 1) << ": "
        << (diagnostic.severity == Severity::Error ? "error: " : "warning: ")
        << diagnostic.message << '\n';
  }
}

} // namespace rx

namespace rx::ast {
namespace {

std::string indent(int level) {
  return std::string(static_cast<std::size_t>(level) * 2, ' ');
}

void dump_node(std::ostringstream &out, const Node &node) {
  out << " #" << node.id << " @" << node.span.line << ':'
      << (node.span.column + 1) << " [" << node.span.begin << ','
      << node.span.end << ')';
}

void dump_type(std::ostringstream &out, const TypePtr &type) {
  // The dump mirrors source-like type spelling for readable snapshots.
  if (!type) {
    out << "()";
    return;
  }
  switch (type->kind) {
  case Type::Kind::Unit:
    out << "()";
    break;
  case Type::Kind::Path:
    for (std::size_t i = 0; i < type->path.size(); ++i) {
      if (i)
        out << "::";
      out << type->path[i];
    }
    if (!type->arguments.empty()) {
      out << '<';
      for (std::size_t i = 0; i < type->arguments.size(); ++i) {
        if (i)
          out << ", ";
        dump_type(out, type->arguments[i]);
      }
      out << '>';
    }
    break;
  case Type::Kind::Reference:
    out << '&' << (type->is_mutable ? "mut " : "");
    dump_type(out, type->element);
    break;
  case Type::Kind::Array:
    out << '[';
    dump_type(out, type->element);
    out << "; " << type->array_length << ']';
    break;
  }
}

void dump_expr(std::ostringstream &out, const ExprPtr &expression, int level);

void dump_stmt(std::ostringstream &out, const std::shared_ptr<Stmt> &statement,
               int level) {
  if (!statement) {
    out << indent(level) << "<missing statement>\n";
    return;
  }
  if (statement->kind == Stmt::Kind::Empty) {
    out << indent(level) << "EmptyStmt";
    dump_node(out, *statement);
    out << '\n';
    return;
  }
  if (statement->kind == Stmt::Kind::Let) {
    out << indent(level) << "Let " << (statement->is_mutable ? "mut " : "")
        << statement->name;
    if (statement->annotation) {
      out << ": ";
      dump_type(out, statement->annotation);
    }
    dump_node(out, *statement);
    out << '\n';
    dump_expr(out, statement->expression, level + 1);
    return;
  }
  out << indent(level) << (statement->has_semicolon ? "ExprStmt" : "TailExpr");
  dump_node(out, *statement);
  out << '\n';
  dump_expr(out, statement->expression, level + 1);
}

void dump_expr(std::ostringstream &out, const ExprPtr &expression, int level) {
  // Child order matches the AST operand order established by the parser.
  if (!expression) {
    out << indent(level) << "<missing>\n";
    return;
  }
  static const char *names[] = {
      "Integer", "Boolean",  "Unit",  "Path",       "Array", "Struct",
      "Block",   "If",       "Loop",  "While",      "Unary", "Binary",
      "Assign",  "Cast",     "Call",  "MethodCall", "Field", "Index",
      "Break",   "Continue", "Return"};
  out << indent(level) << names[static_cast<int>(expression->kind)];
  if (!expression->text.empty())
    out << " " << expression->text;
  if (!expression->path.empty()) {
    out << " path=";
    for (std::size_t i = 0; i < expression->path.size(); ++i) {
      if (i)
        out << "::";
      out << expression->path[i];
    }
  }
  if (expression->cast_type) {
    out << " type=";
    dump_type(out, expression->cast_type);
  }
  if (!expression->type_arguments.empty()) {
    out << " type-args=<";
    for (std::size_t i = 0; i < expression->type_arguments.size(); ++i) {
      if (i)
        out << ", ";
      dump_type(out, expression->type_arguments[i]);
    }
    out << '>';
  }
  if (expression->kind == Expr::Kind::Array && expression->flag)
    out << " repeat";
  dump_node(out, *expression);
  out << '\n';
  for (const auto &field : expression->fields) {
    out << indent(level + 1) << "field " << field.name << '\n';
    dump_expr(out, field.value, level + 2);
  }
  for (const auto &operand : expression->operands)
    dump_expr(out, operand, level + 1);
  for (const auto &statement : expression->statements)
    dump_stmt(out, statement, level + 1);
}

void dump_function(std::ostringstream &out, const Function &function,
                   int level) {
  out << indent(level) << "Function " << function.name;
  if (function.has_self)
    out << " self=" << (function.self_by_ref ? "&" : "")
        << (function.self_mutable ? "mut " : "") << "self";
  dump_node(out, function);
  out << '\n';
  for (const auto &parameter : function.parameters) {
    out << indent(level + 1) << "Parameter "
        << (parameter.is_mutable ? "mut " : "") << parameter.name << ": ";
    dump_type(out, parameter.type);
    dump_node(out, parameter);
    out << '\n';
  }
  out << indent(level + 1) << "Result ";
  dump_type(out, function.result);
  out << '\n';
  dump_expr(out, function.body, level + 1);
}

void dump_constant(std::ostringstream &out, const Constant &constant,
                   int level) {
  out << indent(level) << "Constant " << constant.name << ": ";
  dump_type(out, constant.type);
  dump_node(out, constant);
  out << '\n';
  dump_expr(out, constant.value, level + 1);
}

} // namespace

std::string dump(const Crate &crate) {
  std::ostringstream out;
  out << "Crate";
  dump_node(out, crate);
  out << '\n';
  for (const auto &item : crate.items) {
    std::visit(
        [&](const auto &value) {
          using T = std::decay_t<decltype(value)>;
          if constexpr (std::is_same_v<T, Use>) {
            out << "  Use " << value.text;
            dump_node(out, value);
            out << '\n';
          } else if constexpr (std::is_same_v<T, Struct>) {
            out << "  Struct " << value.name;
            if (!value.derives.empty()) {
              out << " derives=";
              for (std::size_t i = 0; i < value.derives.size(); ++i) {
                if (i)
                  out << ',';
                out << value.derives[i];
              }
            }
            dump_node(out, value);
            out << '\n';
            for (const auto &field : value.fields) {
              out << "    Field " << field.name << ": ";
              dump_type(out, field.type);
              dump_node(out, field);
              out << '\n';
            }
          } else if constexpr (std::is_same_v<T, Function>) {
            dump_function(out, value, 1);
          } else if constexpr (std::is_same_v<T, Constant>) {
            dump_constant(out, value, 1);
          } else if constexpr (std::is_same_v<T, Impl>) {
            out << "  Impl ";
            dump_type(out, value.target);
            dump_node(out, value);
            out << '\n';
            for (const auto &function : value.functions)
              dump_function(out, function, 2);
            for (const auto &constant : value.constants)
              dump_constant(out, constant, 2);
          }
        },
        item);
  }
  return out.str();
}

} // namespace rx::ast
