#pragma once

#include "generated/compact_words_category.hpp"

#include <cstddef>
#include <cstdint>

namespace words::poc::unicode_backend {

using byte_t = std::uint8_t;
using ssize_t = std::ptrdiff_t;

inline constexpr ssize_t invalid_utf8 = -3;

namespace compact {

// Production-shaped subset of utf8proc_iterate. The Words call sites always
// pass an explicit non-negative byte count, so null-terminated mode is outside
// this finite interface.
[[nodiscard]] constexpr ssize_t iterate(const byte_t *input, ssize_t input_size,
                                        codepoint_t *codepoint) noexcept;

// Strict UTF-8 scalar encoder. Production only needs valid Unicode scalars.
[[nodiscard]] ssize_t encode_char(codepoint_t codepoint,
                                  byte_t *output) noexcept;

} // namespace compact

namespace full {

[[nodiscard]] ssize_t iterate(const byte_t *input, ssize_t input_size,
                              codepoint_t *codepoint) noexcept;
[[nodiscard]] ssize_t encode_char(codepoint_t codepoint,
                                  byte_t *output) noexcept;
[[nodiscard]] boundary_flag_t words_category(codepoint_t codepoint) noexcept;

} // namespace full
} // namespace words::poc::unicode_backend

#include "unicode_backend_compact.inl"
