#pragma once

#include "rx/ast.hpp"
#include "rx/diagnostic.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace rx {

// Declaration nodes provide stable symbol identity across frontend stages.
using SymbolId = ast::NodeId;

struct SemanticType;
using SemanticTypePtr = std::shared_ptr<SemanticType>;

struct SemanticType {
  // This type is stable input for later lowering passes.
  enum class Kind {
    Error,
    Never,
    Unit,
    Bool,
    I32,
    U32,
    Isize,
    Usize,
    Struct,
    Box,
    Vec,
    Array,
    Ref,
    Function
  } kind{Kind::Error};
  std::string name;
  SemanticTypePtr element;
  std::uint64_t length{};
  bool is_mutable{};
};

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
  std::string kind;
  std::string name;
  std::optional<SymbolId> owner;
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

struct SemanticResult {
  SemanticSummary summary;
  std::vector<SymbolRecord> symbols;
  std::vector<ExpressionSemantics> expressions;
  // Lowering consumes checked declarations instead of resolving them again.
  std::vector<TypeResolution> resolved_types;
  std::vector<ConstantValue> constants;
};

SemanticResult analyze(const ast::Crate &crate, DiagnosticEngine &diagnostics);
std::string dump_semantic(const SemanticResult &result);

} // namespace rx
