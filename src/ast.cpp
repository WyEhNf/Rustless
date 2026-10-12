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
      visit(predicate,
            Overloaded{[&](const Node &, const LifetimePredicate &p) {
                         indent(level + 1);
                         out_ << p.subject.text;
                         bounds(p.bounds);
                         out_ << '\n';
                       },
                       [&](const Node &, const TypePredicate &p) {
                         indent(level + 1);
                         type(p.subject);
                         bounds(p.bounds);
                         out_ << '\n';
                       }});
  }
  void use_tree(const UseTree &tree) {
    if (tree.prefix.root == PathRoot::Absolute)
      out_ << "::";
    for (std::size_t i = 0; i < tree.prefix.segments.size(); ++i) {
      if (i)
        out_ << "::";
      out_ << name_text(tree.prefix.segments[i]);
    }
    visit(tree,
          Overloaded{[&](const UseTree &, const NameImport &name) {
                       if (name.alias) {
                         out_ << " as ";
                         std::visit(Overloaded{[&](const Identifier &value) {
                                                 out_ << value.text;
                                               },
                                               [&](const UnderscoreAlias &) {
                                                 out_ << '_';
                                               }},
                                    *name.alias);
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
                       for (std::size_t i = 0; i < group.children.size(); ++i) {
                         if (i)
                           out_ << ", ";
                         use_tree(group.children[i]);
                       }
                       out_ << '}';
                     }});
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
      visit(self, Overloaded{[&](const SelfParam &, const ValueSelf &p) {
                               if (p.mutability == Mutability::Mutable)
                                 out_ << "mut ";
                             },
                             [&](const SelfParam &, const ReferenceSelf &p) {
                               out_ << '&';
                               if (p.lifetime)
                                 out_ << p.lifetime->text << ' ';
                               if (p.mutability == Mutability::Mutable)
                                 out_ << "mut ";
                             }});
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
      visit(member,
            [&](const auto &, const auto &p) { declaration(p, level + 1); });
  }
  void expression(const ExprPtr &value, int level);
  void block(const Block &value, int level);
  void conditional(const IfExpr &value, int level);
  std::ostringstream out_;
  const SourceFile &source_;
};

void Printer::block(const Block &value, int level) {
  heading("Block", value, level);
  for (const auto &statement : value.statements)
    visit(*statement,
          Overloaded{[&](const Stmt &node, const EmptyStmt &) {
                       heading("EmptyStmt", node, level + 1);
                     },
                     [&](const Stmt &node, const LetStmt &p) {
                       heading("Let " +
                                   std::string(p.binding.mutability ==
                                                       Mutability::Mutable
                                                   ? "mut "
                                                   : "") +
                                   p.binding.name.text,
                               node, level + 1);
                       if (p.annotation) {
                         indent(level + 2);
                         type(*p.annotation);
                         out_ << '\n';
                       }
                       expression(p.initializer, level + 2);
                     },
                     [&](const Stmt &node, const ExprStmt &p) {
                       heading(p.termination == Termination::Semicolon
                                   ? "ExprStmt"
                                   : "ExprStmt omitted-semicolon",
                               node, level + 1);
                       expression(p.expression, level + 2);
                     }});
  if (value.tail) {
    indent(level + 1);
    out_ << "TailExpr\n";
    expression(*value.tail, level + 2);
  }
}
void Printer::conditional(const IfExpr &value, int level) {
  expression(value.condition, level);
  block(*value.then_block, level);
  if (value.else_branch)
    std::visit(Overloaded{[&](const Box<Block> &p) { block(*p, level); },
                          [&](const Box<IfBranch> &p) {
                            heading("If", *p, level);
                            conditional(p->data, level + 1);
                          }},
               *value.else_branch);
}
void Printer::expression(const ExprPtr &value, int level) {
  auto label = [&](const Expr &node, std::string text) {
    heading(text, node, level);
  };
  auto child = [&](const ExprPtr &p) { expression(p, level + 1); };
  visit(*value,
        Overloaded{
            [&](const Expr &node, const IntegerExpr &p) {
              label(node, "Integer " + p.literal.spelling);
            },
            [&](const Expr &node, const BooleanExpr &p) {
              label(node, p.value ? "Boolean true" : "Boolean false");
            },
            [&](const Expr &node, const UnitExpr &) { label(node, "Unit"); },
            [&](const Expr &node, const PathExpr &p) {
              label(node, "Path");
              indent(level + 1);
              path(p.path);
              out_ << '\n';
            },
            [&](const Expr &node, const ParenExpr &p) {
              label(node, "Paren");
              child(p.inner);
            },
            [&](const Expr &node, const ArrayExpr &p) {
              label(node, "Array");
              for (const auto &element : p.elements)
                child(element);
            },
            [&](const Expr &node, const RepeatArrayExpr &p) {
              label(node, "RepeatArray");
              child(p.element);
              child(p.count);
            },
            [&](const Expr &node, const StructExpr &p) {
              label(node, "StructExpr");
              indent(level + 1);
              path(p.path);
              out_ << '\n';
              for (const auto &field : p.fields) {
                heading("FieldInit " + field.name.text, field, level + 1);
                expression(field.value, level + 2);
              }
            },
            [&](const Expr &node, const BlockExpr &p) {
              label(node, "BlockExpr");
              block(*p.block, level + 1);
            },
            [&](const Expr &node, const IfExpr &p) {
              label(node, "If");
              conditional(p, level + 1);
            },
            [&](const Expr &node, const LoopExpr &p) {
              label(node, "Loop");
              block(*p.body, level + 1);
            },
            [&](const Expr &node, const WhileExpr &p) {
              label(node, "While");
              child(p.condition);
              block(*p.body, level + 1);
            },
            [&](const Expr &node, const UnaryExpr &p) {
              label(node, "Unary " + std::string(spelling(p.op)));
              child(p.operand);
            },
            [&](const Expr &node, const BorrowExpr &p) {
              label(node, p.mutability == Mutability::Mutable ? "Borrow &mut"
                                                              : "Borrow &");
              child(p.operand);
            },
            [&](const Expr &node, const BinaryExpr &p) {
              label(node, "Binary " + std::string(spelling(p.op)));
              child(p.left);
              child(p.right);
            },
            [&](const Expr &node, const AssignExpr &p) {
              label(node, "Assign =");
              child(p.target);
              child(p.value);
            },
            [&](const Expr &node, const CompoundAssignExpr &p) {
              label(node, "CompoundAssign " + std::string(spelling(p.op)));
              child(p.target);
              child(p.value);
            },
            [&](const Expr &node, const CastExpr &p) {
              label(node, "Cast");
              child(p.value);
              indent(level + 1);
              type(p.target_type);
              out_ << '\n';
            },
            [&](const Expr &node, const CallExpr &p) {
              label(node, "Call");
              child(p.callee);
              for (const auto &arg : p.arguments)
                child(arg);
            },
            [&](const Expr &node, const MethodCallExpr &p) {
              label(node, "MethodCall " + p.method.text);
              child(p.receiver);
              if (p.generic_args) {
                indent(level + 1);
                arguments(*p.generic_args);
                out_ << '\n';
              }
              for (const auto &arg : p.arguments)
                child(arg);
            },
            [&](const Expr &node, const FieldExpr &p) {
              label(node, "Field " + p.field.text);
              child(p.base);
            },
            [&](const Expr &node, const IndexExpr &p) {
              label(node, "Index");
              child(p.base);
              child(p.index);
            },
            [&](const Expr &node, const BreakExpr &p) {
              label(node, "Break");
              if (p.value)
                child(*p.value);
            },
            [&](const Expr &node, const ContinueExpr &) {
              label(node, "Continue");
            },
            [&](const Expr &node, const ReturnExpr &p) {
              label(node, "Return");
              if (p.value)
                child(*p.value);
            }});
}

} // namespace
std::string dump(const Crate &crate) { return Printer(crate).print(crate); }
} // namespace rx::ast
