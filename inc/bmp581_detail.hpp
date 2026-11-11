/**
 * @file bmp581_detail.hpp
 * @brief Compile-time helpers of the BMP581 driver: typed integer literals, exact hook matching,
 * typed register fields and enumeration checks.
 * @copyright Copyright (c) 2026 The driver-bmp581 authors. SPDX-License-Identifier: MIT
 *
 * @details
 * Everything here is `constexpr` or used only in unevaluated operands, so it adds no code to the
 * run-time path (handbook C++20 profile, KB-CPP20-006, -008, -020, -021, -025, -026).
 */
#ifndef BMP581_DETAIL_HPP
#define BMP581_DETAIL_HPP

#include <concepts>
#include <cstdint>
#include <limits>
#include <type_traits>

namespace bmp581::detail {

/**
 * @brief Reached only when a `_u8`, `_u16` or `_u32` literal does not fit its type: it is never
 * defined and not `constexpr`, so the constant evaluation fails and the build names it.
 */
void LiteralOutOfRange() noexcept;

/**
 * @brief An integer constant of type `uint8_t`, for an argument whose parameter is `uint8_t`
 * (MISRA C++:2023 Rule 7.0.6). `0xFF_u8` compiles; `0x100_u8` does not.
 * @param v The literal's value.
 * @return `v` as `uint8_t`.
 */
consteval std::uint8_t operator""_u8(unsigned long long v) noexcept {
  if (v > 0xFFULL) {
    LiteralOutOfRange();
  }
  return static_cast<std::uint8_t>(v);
}

/**
 * @brief An integer constant of type `uint16_t` (Rule 7.0.6); out of range does not compile.
 * @param v The literal's value.
 * @return `v` as `uint16_t`.
 */
consteval std::uint16_t operator""_u16(unsigned long long v) noexcept {
  if (v > 0xFFFFULL) {
    LiteralOutOfRange();
  }
  return static_cast<std::uint16_t>(v);
}

/**
 * @brief An integer constant of type `uint32_t` (Rule 7.0.6): on targets where `uint32_t` is
 * `unsigned long`, a `U` literal is still `unsigned int`. Out of range does not compile.
 * @param v The literal's value.
 * @return `v` as `uint32_t`.
 */
consteval std::uint32_t operator""_u32(unsigned long long v) noexcept {
  if (v > 0xFFFF'FFFFULL) {
    LiteralOutOfRange();
  }
  return static_cast<std::uint32_t>(v);
}

/**
 * @brief Matches a port hook against exactly one signature, `R(Args...) noexcept`, as a member
 * function of `Port` (`const` or not) (KB-CPP20-006).
 *
 * `ExactHook<Port, Sig>::Take(&Port::Hook)` is well-formed only when `&Port::Hook` names a member
 * function of exactly that type: the address of an overloaded or template hook resolves to the
 * one that matches, and a hook with a narrower or different parameter, another result type or no
 * `noexcept` has no match. No value is converted. Declared only: it appears in
 * requires-expressions, never in evaluated code.
 *
 * @tparam Port The port class.
 * @tparam Sig The hook's function type, `noexcept`; a `Sig` without `noexcept` has no definition.
 */
template <typename Port, typename Sig>
  requires std::is_class_v<Port> && std::is_function_v<Sig>
struct ExactHook;

/// The `noexcept` signatures: the only ones a hook may have.
template <typename Port, typename R, typename... Args>
  requires std::is_class_v<Port>
struct ExactHook<Port, R(Args...) noexcept> {
  /// Takes the address of a non-`const` hook of exactly this signature.
  static void Take(R (Port::*hook)(Args...) noexcept) noexcept;
  /// Takes the address of a `const` hook of exactly this signature.
  static void Take(R (Port::*hook)(Args...) const noexcept) noexcept;
};

/// A class type: what `kAlwaysFalse` depends on, the port or another class of the branch.
template <typename T>
concept ClassType = std::is_class_v<T>;

/// `false` for every class `T`: the condition of a `static_assert` in a branch that must not be
/// instantiated (KB-CPP20-008).
template <ClassType>
inline constexpr bool kAlwaysFalse = false;

/// A scoped or unscoped enumeration: what the enumeration helpers take (KB-CPP20-005, -026).
template <typename E>
concept Enumeration = std::is_enum_v<E>;

/**
 * @brief The value of an enumerator in its underlying type, our stand-in for C++23
 * `std::to_underlying` (KB-CPP20-002, -026).
 */
template <Enumeration E>
[[nodiscard]] constexpr std::underlying_type_t<E> ToUnderlying(E e) noexcept {
  return static_cast<std::underlying_type_t<E>>(e);
}

/**
 * @brief The last enumerator of a contiguous enumeration that starts at 0; specialised next to each
 * enumeration that `IsEnumerator()` checks (KB-CPP20-026).
 */
template <Enumeration E>
struct EnumLast;

/**
 * @brief Whether `e` holds an enumerator of a contiguous enumeration that starts at 0. A value made
 * by `static_cast` from an integer can be anything its underlying type holds (KB-CPP20-025).
 */
template <Enumeration E>
[[nodiscard]] constexpr bool IsEnumerator(E e) noexcept {
  return static_cast<uint32_t>(ToUnderlying(e)) <=
         static_cast<uint32_t>(ToUnderlying(EnumLast<E>::kValue));
}

/**
 * @brief A bit field of a register: `Width` bits from bit `Shift` of a `Reg`. Every shift is a
 * constant below the width and every operand is widened to `uint32_t` once, by construction
 * (KB-CPP20-020, -021).
 */
template <typename Reg, uint32_t Shift, uint32_t Width>
  requires std::unsigned_integral<Reg> && (Width > 0U) && (Width < 32U) &&
           ((Shift + Width) <= static_cast<uint32_t>(std::numeric_limits<Reg>::digits))
struct Field {
  /// The largest value the field holds.
  static constexpr uint32_t kMax = (uint32_t{1} << Width) - 1U;
  /// The field's bits in the register.
  static constexpr Reg kMask = static_cast<Reg>(kMax << Shift);

  /// `reg` with the field set to `value` (masked to the field's width); the other bits unchanged.
  [[nodiscard]] static constexpr Reg Insert(Reg reg, uint32_t value) noexcept {
    return static_cast<Reg>((static_cast<uint32_t>(reg) & ~static_cast<uint32_t>(kMask)) |
                            ((value & kMax) << Shift));
  }
  /// The field's value in `reg`.
  [[nodiscard]] static constexpr uint32_t Extract(Reg reg) noexcept {
    return (static_cast<uint32_t>(reg) >> Shift) & kMax;
  }
};

} // namespace bmp581::detail

#endif // BMP581_DETAIL_HPP
