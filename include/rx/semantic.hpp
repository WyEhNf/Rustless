#pragma once

#include "rx/ast.hpp"
#include "rx/diagnostic.hpp"
#include "rx/semantic_operation.hpp"
#include "rx/semantic_type.hpp"
#include "rx/symbol.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace rx {

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

struct SymbolRecord {
  SymbolId id{};
  SymbolKind kind;
  std::string name;
  std::optional<SymbolId> owner;
  ast::NodeId declaration;
};

struct ExpressionSemantics {

  ast::NodeId node{};
  SemanticTypePtr type;
  bool is_place{};
  bool is_mutable{};
  bool access_locked{};
  std::optional<SymbolId> symbol;
  std::string target;
  std::vector<Adjustment> adjustments;
  std::optional<CallTarget> callable;
  std::optional<ResolvedBinary> binary;
  std::optional<ResolvedBinary> compound;
  std::optional<ResolvedUnary> unary;

  std::optional<OperandSemantics> receiver;
};

struct SemanticSummary {

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

  std::vector<SymbolId> fields;
};

struct SemanticData {

  const ast::Crate *source_crate{};
  SemanticSummary summary;
  std::vector<SymbolRecord> symbols;
  std::vector<ExpressionSemantics> expressions;

  std::vector<TypeResolution> resolved_types;
  std::vector<ConstantValue> constants;
  std::vector<SymbolType> symbol_types;
  std::vector<FunctionSemantics> functions;
  std::vector<StructSemantics> structures;
  bool success{};
};

class SemanticResult final {
public:
  SemanticResult();
  const SemanticData &data() const { return *data_; }
  std::shared_ptr<const SemanticData> snapshot() const { return data_; }
  bool success() const { return data_->success; }
  static SemanticResult publish(const ast::Crate &crate, SemanticData data);
  static SemanticResult rejected(const ast::Crate &crate, SemanticData data);
  static SemanticResult publish(ast::Crate &&, SemanticData) = delete;
  static SemanticResult publish(const ast::Crate &&, SemanticData) = delete;
  static SemanticResult rejected(ast::Crate &&, SemanticData) = delete;
  static SemanticResult rejected(const ast::Crate &&, SemanticData) = delete;

private:
  explicit SemanticResult(std::shared_ptr<const SemanticData> data)
      : data_(std::move(data)) {}
  std::shared_ptr<const SemanticData> data_;
};

SemanticResult analyze(const ast::Crate &crate, DiagnosticEngine &diagnostics);
std::string dump_semantic(const SemanticResult &result);

}
