#include "rx/ast/ast.hpp"

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
  for (const auto &diagnostic : diagnostics_) {
    if (diagnostic.severity == Severity::Error)
      ++count;
  }
  return count;
}

const std::vector<Diagnostic> &DiagnosticEngine::diagnostics() const { return diagnostics_; }

void DiagnosticEngine::print(std::ostream &out) const {
  for (const auto &diagnostic : diagnostics_) {
    if (!file_.empty())
      out << file_ << ':';
    out << diagnostic.span.line << ':' << (diagnostic.span.column + 1) << ": ";
    if (diagnostic.severity == Severity::Error)
      out << "error: ";
    else
      out << "warning: ";
    out << diagnostic.message << '\n';
  }
}

}

namespace rx::ast {
namespace {
class Printer {
public:
  explicit Printer(const Crate &crate) : source_(crate.source) {}
  std::string print(const Crate &crate) {
    heading("Crate", crate, 0);
    for (const auto &item : crate.items)
      visit(item, [&](const auto &, const auto &value) { declaration(value, 1); });
    return out_.str();
  }

private:
  void indent(int level) { out_ << std::string(static_cast<std::size_t>(level) * 2, ' '); }
  void heading(std::string_view name, const Node &node, int level) {
    indent(level);
    out_ << name;
    out_ << " #" << node.id << " @" << node.span.line << ':' << node.span.column + 1 << " ["
         << node.span.begin << ',' << node.span.end << ")\n";
  }
  void arguments(const GenericArguments &generic_arguments) {
    out_ << '<';
    auto print_argument = Overloaded{
        [&](const TypePtr &value) { type(value); },
        [&](const LifetimeName &value) { out_ << value.text; },
    };
    for (std::size_t index = 0; index < generic_arguments.values.size(); ++index) {
      if (index)
        out_ << ", ";
      std::visit(print_argument, generic_arguments.values[index]);
    }
    out_ << '>';
  }
  void path(const Path &value) {
    for (std::size_t index = 0; index < value.segments.size(); ++index) {
      if (index)
        out_ << "::";
      const auto &segment = value.segments[index];
      out_ << name_text(segment.name);
      if (segment.arguments)
        arguments(*segment.arguments);
    }
  }
  void type(const TypePtr &syntax) {
    auto print_type = Overloaded{
        [&](const TypeSyntax &, const UnitType &) { out_ << "()"; },
        [&](const TypeSyntax &, const PathType &value) { path(value.path); },
        [&](const TypeSyntax &, const ReferenceType &value) {
          out_ << '&';
          if (value.lifetime)
            out_ << value.lifetime->text << ' ';
          if (value.mutability == Mutability::Mutable)
            out_ << "mut ";
          type(value.referent);
        },
        [&](const TypeSyntax &, const ArrayType &value) {
          out_ << '[';
          type(value.element);
          out_ << "; " << source_.slice(value.length->span) << ']';
        },
        [&](const TypeSyntax &, const ParenType &value) {
          out_ << '(';
          type(value.inner);
          out_ << ')';
        },
    };
    visit(*syntax, print_type);
  }
  void generics(const std::optional<GenericParameters> &value) {
    if (!value)
      return;
    out_ << '<';
    for (std::size_t index = 0; index < value->lifetimes.size(); ++index) {
      if (index)
        out_ << ", ";
      const auto &parameter = value->lifetimes[index];
      out_ << parameter.name.text;
      bounds(parameter.bounds);
    }
    out_ << '>';
  }
  void bounds(const std::vector<LifetimeName> &values) {
    for (std::size_t index = 0; index < values.size(); ++index) {
      if (index == 0)
        out_ << ": ";
      else
        out_ << " + ";
      out_ << values[index].text;
    }
  }
  void where_clause(const std::optional<WhereClause> &value, int level) {
    if (!value)
      return;
    indent(level);
    out_ << "Where\n";
    auto print_predicate = Overloaded{
        [&](const Node &, const LifetimePredicate &payload) {
          indent(level + 1);
          out_ << payload.subject.text;
          bounds(payload.bounds);
          out_ << '\n';
        },
        [&](const Node &, const TypePredicate &payload) {
          indent(level + 1);
          type(payload.subject);
          bounds(payload.bounds);
          out_ << '\n';
        },
    };
    for (const auto &predicate : value->predicates)
      visit(predicate, print_predicate);
  }
  void use_tree(const UseTree &tree) {
    if (tree.prefix.root == PathRoot::Absolute)
      out_ << "::";
    for (std::size_t index = 0; index < tree.prefix.segments.size(); ++index) {
      if (index)
        out_ << "::";
      out_ << name_text(tree.prefix.segments[index]);
    }
    auto print_import = Overloaded{
        [&](const UseTree &, const NameImport &name) {
          if (name.alias) {
            out_ << " as ";
            auto print_alias = Overloaded{
                [&](const Identifier &value) { out_ << value.text; },
                [&](const UnderscoreAlias &) { out_ << '_'; },
            };
            std::visit(print_alias, *name.alias);
          }
        },
        [&](const UseTree &owner, const GlobImport &) {
          if (!owner.prefix.segments.empty())
            out_ << "::";
          out_ << '*';
        },
        [&](const UseTree &owner, const GroupImport &group) {
          if (!owner.prefix.segments.empty())
            out_ << "::";
          out_ << '{';
          for (std::size_t index = 0; index < group.children.size(); ++index) {
            if (index)
              out_ << ", ";
            use_tree(group.children[index]);
          }
          out_ << '}';
        },
    };
    visit(tree, print_import);
  }
  void declaration(const Use &value, int level) {
    heading("Use", value, level);
    indent(level + 1);
    use_tree(value.tree);
    out_ << '\n';
  }
  void declaration(const Function &value, int level) {
    heading("Function " + value.name.text, value, level);
    if (value.generics) {
      indent(level + 1);
      out_ << "Generics ";
      generics(value.generics);
      out_ << '\n';
    }
    if (value.signature.receiver) {
      const auto &self = *value.signature.receiver;
      indent(level + 1);
      out_ << "Self ";
      auto print_receiver = Overloaded{
          [&](const SelfParam &, const ValueSelf &payload) {
            if (payload.mutability == Mutability::Mutable)
              out_ << "mut ";
          },
          [&](const SelfParam &, const ReferenceSelf &payload) {
            out_ << '&';
            if (payload.lifetime)
              out_ << payload.lifetime->text << ' ';
            if (payload.mutability == Mutability::Mutable)
              out_ << "mut ";
          },
      };
      visit(self, print_receiver);
      out_ << "self\n";
    }
    for (const auto &parameter : value.signature.parameters) {
      heading("Parameter " + parameter.binding.name.text, parameter, level + 1);
      indent(level + 2);
      type(parameter.type);
      out_ << '\n';
    }
    if (value.signature.return_annotation) {
      indent(level + 1);
      out_ << "Result ";
      type(*value.signature.return_annotation);
      out_ << '\n';
    }
    where_clause(value.where_clause, level + 1);
    block(*value.body, level + 1);
  }
  void declaration(const Constant &value, int level) {
    heading("Constant " + value.name.text, value, level);
    indent(level + 1);
    type(value.type);
    out_ << '\n';
    expression(value.value, level + 1);
  }
  void declaration(const Struct &value, int level) {
    heading("Struct " + value.name.text, value, level);
    if (value.generics) {
      indent(level + 1);
      generics(value.generics);
      out_ << '\n';
    }
    for (const auto &attribute : value.attributes) {
      indent(level + 1);
      out_ << "Derive";
      for (const auto &name : attribute.names)
        out_ << ' ' << spelling(name.kind);
      out_ << '\n';
    }
    where_clause(value.where_clause, level + 1);
    for (const auto &field : value.fields) {
      heading("Field " + field.name.text, field, level + 1);
      indent(level + 2);
      type(field.type);
      out_ << '\n';
    }
  }
  void declaration(const Impl &value, int level) {
    heading("Impl", value, level);
    indent(level + 1);
    type(value.target);
    out_ << '\n';
    if (value.generics) {
      indent(level + 1);
      generics(value.generics);
      out_ << '\n';
    }
    where_clause(value.where_clause, level + 1);
    for (const auto &member : value.members)
      visit(member, [&](const auto &, const auto &payload) { declaration(payload, level + 1); });
  }
  void expression(const ExprPtr &value, int level);
  void block(const Block &value, int level);
  void conditional(const IfExpr &value, int level);
  std::ostringstream out_;
  const SourceFile &source_;
};

void Printer::block(const Block &value, int level) {
  heading("Block", value, level);
  auto print_statement = Overloaded{
      [&](const Stmt &node, const EmptyStmt &) { heading("EmptyStmt", node, level + 1); },
      [&](const Stmt &node, const LetStmt &payload) {
        std::string label = "Let ";
        if (payload.binding.mutability == Mutability::Mutable)
          label += "mut ";
        label += payload.binding.name.text;
        heading(label, node, level + 1);
        if (payload.annotation) {
          indent(level + 2);
          type(*payload.annotation);
          out_ << '\n';
        }
        expression(payload.initializer, level + 2);
      },
      [&](const Stmt &node, const ExprStmt &payload) {
        std::string label = "ExprStmt";
        if (payload.termination == Termination::Omitted)
          label += " omitted-semicolon";
        heading(label, node, level + 1);
        expression(payload.expression, level + 2);
      },
  };
  for (const auto &statement : value.statements)
    visit(*statement, print_statement);
  if (value.tail) {
    indent(level + 1);
    out_ << "TailExpr\n";
    expression(*value.tail, level + 2);
  }
}
void Printer::conditional(const IfExpr &value, int level) {
  expression(value.condition, level);
  block(*value.then_block, level);
  if (value.else_branch) {
    auto print_else = Overloaded{
        [&](const Box<Block> &payload) { block(*payload, level); },
        [&](const Box<IfBranch> &payload) {
          heading("If", *payload, level);
          conditional(payload->data, level + 1);
        },
    };
    std::visit(print_else, *value.else_branch);
  }
}
void Printer::expression(const ExprPtr &value, int level) {
  auto label = [&](const Expr &node, std::string text) { heading(text, node, level); };
  auto child = [&](const ExprPtr &payload) { expression(payload, level + 1); };
  auto print_expression = Overloaded{
      [&](const Expr &node, const IntegerExpr &payload) {
        label(node, "Integer " + payload.literal.spelling);
      },
      [&](const Expr &node, const BooleanExpr &payload) {
        if (payload.value)
          label(node, "Boolean true");
        else
          label(node, "Boolean false");
      },
      [&](const Expr &node, const UnitExpr &) { label(node, "Unit"); },
      [&](const Expr &node, const PathExpr &payload) {
        label(node, "Path");
        indent(level + 1);
        path(payload.path);
        out_ << '\n';
      },
      [&](const Expr &node, const ParenExpr &payload) {
        label(node, "Paren");
        child(payload.inner);
      },
      [&](const Expr &node, const ArrayExpr &payload) {
        label(node, "Array");
        for (const auto &element : payload.elements)
          child(element);
      },
      [&](const Expr &node, const RepeatArrayExpr &payload) {
        label(node, "RepeatArray");
        child(payload.element);
        child(payload.count);
      },
      [&](const Expr &node, const StructExpr &payload) {
        label(node, "StructExpr");
        indent(level + 1);
        path(payload.path);
        out_ << '\n';
        for (const auto &field : payload.fields) {
          heading("FieldInit " + field.name.text, field, level + 1);
          expression(field.value, level + 2);
        }
      },
      [&](const Expr &node, const BlockExpr &payload) {
        label(node, "BlockExpr");
        block(*payload.block, level + 1);
      },
      [&](const Expr &node, const IfExpr &payload) {
        label(node, "If");
        conditional(payload, level + 1);
      },
      [&](const Expr &node, const LoopExpr &payload) {
        label(node, "Loop");
        block(*payload.body, level + 1);
      },
      [&](const Expr &node, const WhileExpr &payload) {
        label(node, "While");
        child(payload.condition);
        block(*payload.body, level + 1);
      },
      [&](const Expr &node, const UnaryExpr &payload) {
        label(node, "Unary " + std::string(spelling(payload.op)));
        child(payload.operand);
      },
      [&](const Expr &node, const BorrowExpr &payload) {
        if (payload.mutability == Mutability::Mutable)
          label(node, "Borrow &mut");
        else
          label(node, "Borrow &");
        child(payload.operand);
      },
      [&](const Expr &node, const BinaryExpr &payload) {
        label(node, "Binary " + std::string(spelling(payload.op)));
        child(payload.left);
        child(payload.right);
      },
      [&](const Expr &node, const AssignExpr &payload) {
        label(node, "Assign =");
        child(payload.target);
        child(payload.value);
      },
      [&](const Expr &node, const CompoundAssignExpr &payload) {
        label(node, "CompoundAssign " + std::string(spelling(payload.op)));
        child(payload.target);
        child(payload.value);
      },
      [&](const Expr &node, const CastExpr &payload) {
        label(node, "Cast");
        child(payload.value);
        indent(level + 1);
        type(payload.target_type);
        out_ << '\n';
      },
      [&](const Expr &node, const CallExpr &payload) {
        label(node, "Call");
        child(payload.callee);
        for (const auto &argument : payload.arguments)
          child(argument);
      },
      [&](const Expr &node, const MethodCallExpr &payload) {
        label(node, "MethodCall " + payload.method.text);
        child(payload.receiver);
        if (payload.generic_args) {
          indent(level + 1);
          arguments(*payload.generic_args);
          out_ << '\n';
        }
        for (const auto &argument : payload.arguments)
          child(argument);
      },
      [&](const Expr &node, const FieldExpr &payload) {
        label(node, "Field " + payload.field.text);
        child(payload.base);
      },
      [&](const Expr &node, const IndexExpr &payload) {
        label(node, "Index");
        child(payload.base);
        child(payload.index);
      },
      [&](const Expr &node, const BreakExpr &payload) {
        label(node, "Break");
        if (payload.value)
          child(*payload.value);
      },
      [&](const Expr &node, const ContinueExpr &) { label(node, "Continue"); },
      [&](const Expr &node, const ReturnExpr &payload) {
        label(node, "Return");
        if (payload.value)
          child(*payload.value);
      },
  };
  visit(*value, print_expression);
}

}
std::string dump(const Crate &crate) { return Printer(crate).print(crate); }
}
