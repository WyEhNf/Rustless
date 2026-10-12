#pragma once

#include "rx/ast.hpp"
#include "rx/diagnostic.hpp"
#include "rx/symbol.hpp"
#include "rx/semantic_type.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace rx {

// Syntax and semantic identities are distinct, even when numerically related.
enum class SymbolKind {
  Struct,
  Function,
  Constant,
  AssociatedFunction,
  AssociatedConstant,
  Field,
  Parameter,
  Receiver,
  Local
};
std::string_view symbol_kind_name(SymbolKind kind);

struct Adjustment {
  // Implicit operations are stored in execution order.
  enum class Kind {
    Dereference,
    BorrowShared,
    BorrowMutable,
    MutToShared,
    NeverToAny
  } kind{Kind::Dereference};
  SemanticTypePtr target;
};

struct SymbolRecord {
  SymbolId id{};
  SymbolKind kind;
  std::string name;
  std::optional<SymbolId> owner;
  ast::NodeId declaration;
};

struct ExpressionSemantics {
  // Method-call adjustments describe how its receiver is passed.
  ast::NodeId node{};
  SemanticTypePtr type;
  bool is_place{};
  bool is_mutable{};
  bool access_locked{};
  std::optional<SymbolId> symbol;
  std::string target;
  std::vector<Adjustment> adjustments;
};

struct SemanticSummary {
  // Counts describe declarations accepted for this compilation unit.
  std::size_t functions{};
  std::size_t structures{};
  std::size_t constants{};
};

struct TypeResolution {
  ast::NodeId node{};
  SemanticTypePtr type;
};

struct ConstantValue {
  SymbolId symbol{};
  std::uint32_t bits{};
};

struct SymbolType {
  SymbolId symbol;
  SemanticTypePtr type;
};
struct ParameterSemantics {
  SymbolId symbol;
  SemanticTypePtr type;
};
struct FunctionSemantics {
  SymbolId symbol;
  std::optional<ParameterSemantics> receiver;
  std::vector<ParameterSemantics> parameters;
  SemanticTypePtr result;
};
struct StructSemantics {
  SymbolId symbol;
  // Preserve declaration order for aggregate layout, not hash-table order.
  std::vector<SymbolId> fields;
};

struct SemanticData {
  // Borrowed identity only; clients must keep this crate alive for AST queries.
  const ast::Crate *source_crate{};
  SemanticSummary summary;
  std::vector<SymbolRecord> symbols;
  std::vector<ExpressionSemantics> expressions;
  // Lowering consumes checked declarations instead of resolving them again.
  std::vector<TypeResolution> resolved_types;
  std::vector<ConstantValue> constants;
  std::vector<SymbolType> symbol_types;
  std::vector<FunctionSemantics> functions;
  std::vector<StructSemantics> structures;
  bool success{};
};

// Consumers receive a frozen snapshot only after all facts have been checked.
class SemanticResult final {
public:
  SemanticResult();
  const SemanticData &data() const { return *data_; }
  std::shared_ptr<const SemanticData> snapshot() const { return data_; }
  bool success() const { return data_->success; }
  static SemanticResult publish(const ast::Crate &crate, SemanticData data);
  static SemanticResult rejected(const ast::Crate &crate, SemanticData data);

private:
  explicit SemanticResult(std::shared_ptr<const SemanticData> data)
      : data_(std::move(data)) {}
  std::shared_ptr<const SemanticData> data_;
};

SemanticResult analyze(const ast::Crate &crate, DiagnosticEngine &diagnostics);
std::string dump_semantic(const SemanticResult &result);

} // namespace rx
