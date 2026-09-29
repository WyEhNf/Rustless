#include "rx/semantic.hpp"

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <functional>
#include <iterator>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <sstream>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

namespace rx {
namespace {

using Ty = SemanticType;
using TyPtr = SemanticTypePtr;

TyPtr ty(Ty::Kind kind) {
  auto value = std::make_shared<Ty>();
  value->kind = kind;
  return value;
}

TyPtr compound(Ty::Kind kind, TyPtr element, bool is_mutable = false) {
  auto value = ty(kind);
  value->element = std::move(element);
  value->is_mutable = is_mutable;
  return value;
}

bool same(const TyPtr &lhs, const TyPtr &rhs) {
  // Error types compare successfully to suppress follow-on diagnostics.
  if (!lhs || !rhs)
    return false;
  if (lhs->kind == Ty::Kind::Error || rhs->kind == Ty::Kind::Error)
    return true;
  if (lhs->kind != rhs->kind)
    return false;
  if (lhs->kind == Ty::Kind::Struct || lhs->kind == Ty::Kind::Function)
    return lhs->name == rhs->name;
  if (lhs->kind == Ty::Kind::Array)
    return lhs->length == rhs->length && same(lhs->element, rhs->element);
  if (lhs->kind == Ty::Kind::Box || lhs->kind == Ty::Kind::Vec)
    return same(lhs->element, rhs->element);
  if (lhs->kind == Ty::Kind::Ref)
    return lhs->is_mutable == rhs->is_mutable &&
           same(lhs->element, rhs->element);
  return true;
}

bool integer(const TyPtr &value) {
  return value &&
         (value->kind == Ty::Kind::I32 || value->kind == Ty::Kind::U32 ||
          value->kind == Ty::Kind::Isize || value->kind == Ty::Kind::Usize);
}

bool signed_integer(const TyPtr &value) {
  return value &&
         (value->kind == Ty::Kind::I32 || value->kind == Ty::Kind::Isize);
}

std::string type_name(const TyPtr &value) {
  if (!value)
    return "<missing>";
  switch (value->kind) {
  case Ty::Kind::Error:
    return "<error>";
  case Ty::Kind::Never:
    return "!";
  case Ty::Kind::Unit:
    return "()";
  case Ty::Kind::Bool:
    return "bool";
  case Ty::Kind::I32:
    return "i32";
  case Ty::Kind::U32:
    return "u32";
  case Ty::Kind::Isize:
    return "isize";
  case Ty::Kind::Usize:
    return "usize";
  case Ty::Kind::Struct:
    return value->name;
  case Ty::Kind::Box:
    return "Box<" + type_name(value->element) + ">";
  case Ty::Kind::Vec:
    return "Vec<" + type_name(value->element) + ">";
  case Ty::Kind::Array:
    return "[" + type_name(value->element) + "; " +
           std::to_string(value->length) + "]";
  case Ty::Kind::Ref:
    return std::string("&") + (value->is_mutable ? "mut " : "") +
           type_name(value->element);
  case Ty::Kind::Function:
    return "fn " + value->name;
  }
  return "<?>";
}

struct ConstantInfo {
  const ast::Constant *ast{};
  TyPtr type;
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
  std::unordered_map<std::string, TyPtr> fields;
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
  std::vector<TyPtr> parameters;
  TyPtr result;
  std::string owner;
};

struct ExprInfo {
  ExprInfo(TyPtr type = ty(Ty::Kind::Error), bool is_place = false,
           bool is_mutable = false, bool access_locked = false,
           std::optional<SymbolId> symbol = {}, std::string target = {},
           std::vector<Adjustment> adjustments = {})
      : type(std::move(type)), is_place(is_place), is_mutable(is_mutable),
        access_locked(access_locked), symbol(symbol), target(std::move(target)),
        adjustments(std::move(adjustments)) {}

  TyPtr type;
  bool is_place{};
  bool is_mutable{};
  bool access_locked{};
  std::optional<SymbolId> symbol;
  std::string target;
  std::vector<Adjustment> adjustments;
};

struct Binding {
  TyPtr type;
  bool is_mutable{};
  SymbolId symbol{};
};

