#pragma once

#include "words/lexer.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <limits>
#include <span>
#include <string_view>
#include <type_traits>

#if defined(WORDS_UNICODE_BACKEND_COMPACT)
#include "whitakers-words/poc/latin-unicode/unicode_backend.hpp"
#elif defined(WORDS_UNICODE_BACKEND_FULL)
#include <utf8proc.h>
#else
#error "The Unicode backend must be selected by CMake"
#endif

namespace words::detail::unicode_backend {

using codepoint_t = std::int32_t;
using byte_t = std::uint8_t;
using ssize_t = std::ptrdiff_t;

struct DecodedCodepoint final {
    codepoint_t value{};
    std::size_t byte_count{1U};
    bool valid{};
};

// Keep the UTF-8 cursor on the raw pointer/length ABI. The compact decoder
// itself is always-inline; this seam remains a normal inline function so the
// optimizer can trade call overhead against duplication at its three users.
[[nodiscard]] inline ssize_t iterate(const byte_t *const input,
                                     const ssize_t input_size,
                                     codepoint_t *const codepoint) noexcept {
#if defined(WORDS_UNICODE_BACKEND_COMPACT)
    return poc::unicode_backend::compact::iterate(input, input_size, codepoint);
#else
    static_assert(std::is_same_v<byte_t, utf8proc_uint8_t>);
    static_assert(std::is_same_v<codepoint_t, utf8proc_int32_t>);
    static_assert(std::is_same_v<ssize_t, utf8proc_ssize_t>);
    return utf8proc_iterate(input, input_size, codepoint);
#endif
}

[[nodiscard]] inline DecodedCodepoint
decode_at(const std::span<const byte_t> bytes,
          const std::size_t byte_offset) noexcept {
    const auto remaining = bytes.subspan(byte_offset);
    const auto available = static_cast<ssize_t>(std::min(
        remaining.size(),
        static_cast<std::size_t>(std::numeric_limits<ssize_t>::max())));
    codepoint_t codepoint{};
    const auto consumed = iterate(remaining.data(), available, &codepoint);
    if (consumed <= 0) {
        return {};
    }
    return DecodedCodepoint{.value = codepoint,
                            .byte_count = static_cast<std::size_t>(consumed),
                            .valid = true};
}

// Application-oriented replacement for the utf8proc category block used by
// TextTokenCursor. It deliberately exposes BoundaryFlag, not Unicode's general
// category vocabulary.
[[nodiscard]] BoundaryFlag words_category(codepoint_t codepoint) noexcept;

#if defined(WORDS_UNICODE_BACKEND_COMPACT)
// The ASCII fast path remains in LatinLexer. Only the Unicode path enters the
// finite Latin normalizer selected for compact builds.
[[nodiscard]] std::expected<SurfaceForm, LexError>
normalize_latin(std::string_view input);
#endif

} // namespace words::detail::unicode_backend
