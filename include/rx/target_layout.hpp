#pragma once

#include <cstdint>
#include <limits>
#include <string_view>

namespace rx::ir {
struct Layout {
  std::uint32_t bytes;
  std::uint32_t alignment;
};
struct VecLayout {
  Layout storage;
  std::uint32_t data_offset;
  std::uint32_t length_offset;
  std::uint32_t capacity_offset;
};

// Only RV32 is supported. This describes storage and instruction widths;
// aggregate argument passing is a separate internal compiler convention.
class TargetLayout {
public:
  static constexpr TargetLayout rv32() { return TargetLayout{}; }
  constexpr unsigned integer_bits() const { return 32; }
  constexpr Layout integer() const {
    return {integer_bits() / 8, integer_bits() / 8};
  }
  constexpr Layout pointer() const { return integer(); }
  constexpr Layout boolean() const { return {1, 1}; }
  constexpr Layout unit() const { return {0, 1}; }
  constexpr VecLayout vector() const {
    const auto word = integer();
    return {{pointer().bytes + 2 * word.bytes, word.alignment},
            0,
            pointer().bytes,
            pointer().bytes + word.bytes};
  }
  constexpr std::uint32_t shift_count_mask() const {
    return integer_bits() - 1;
  }
  constexpr std::uint64_t max_object_size() const {
    return std::numeric_limits<std::uint32_t>::max();
  }
  constexpr std::string_view triple() const { return "riscv32-unknown-elf"; }
  constexpr std::string_view llvm_data_layout() const {
    return "e-m:e-p:32:32-i64:64-n32-S128";
  }

private:
  constexpr TargetLayout() = default;
};
} // namespace rx::ir
