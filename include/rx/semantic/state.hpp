#pragma once

#include "rx/semantic/semantic.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace rx::semantic_detail {

SemanticTypePtr error_type();
bool compatible_types(const SemanticTypePtr &lhs, const SemanticTypePtr &rhs);
bool integer(const SemanticTypePtr &value);
bool signed_integer(const SemanticTypePtr &value);

struct ConstantInfo {
  const ast::Constant *ast{};
  SemanticTypePtr type;
  std::string owner;
  enum class TypeState {
    Fresh,
    Resolving,
    Done,
    Failed
  } type_state{TypeState::Fresh};
  enum class State { Fresh, Evaluating, Done, Failed } state{State::Fresh};
  std::int64_t value{};
};

struct StructInfo {
  const ast::Struct *ast{};
  std::unordered_map<std::string, SemanticTypePtr> fields;
  std::unordered_map<std::string, SymbolId> field_symbols;
  std::unordered_set<std::string> derives;
  enum class LayoutState {
    Fresh,
    Visiting,
    Valid,
    Invalid
  } layout{LayoutState::Fresh};
};

struct FunctionInfo {
  const ast::Function *ast{};
  std::vector<SemanticTypePtr> parameters;
  SemanticTypePtr result;
  std::string owner;
};

struct ExprInfo {
  ExprInfo(SemanticTypePtr type = error_type(), bool is_place = false,
           bool is_mutable = false, bool access_locked = false,
           std::optional<SymbolId> symbol = {}, std::string target = {},
           std::vector<Adjustment> adjustments = {})
      : type(std::move(type)), is_place(is_place), is_mutable(is_mutable),
        access_locked(access_locked), symbol(symbol), target(std::move(target)),
        adjustments(std::move(adjustments)) {
    if (auto *function = rx::get_if<FunctionType>(*this->type))
      callable = FunctionTarget{function->symbol};
    else if (auto *builtin = rx::get_if<BuiltinFunctionType>(*this->type))
      callable = BuiltinTarget{builtin->builtin};
  }

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

struct Binding {
  SemanticTypePtr type;
  bool is_mutable{};
  SymbolId symbol{};
};

} // namespace rx::semantic_detail
