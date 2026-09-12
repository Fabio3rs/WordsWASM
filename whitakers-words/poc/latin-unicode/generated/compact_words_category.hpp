#pragma once

#include <cstdint>
#include <string_view>

namespace words::poc::unicode_backend {

using codepoint_t = std::int32_t;

// Values intentionally mirror words::BoundaryFlag. The generated compact
// backend stays independent of production headers; differential tests assert
// that these bits have not drifted.
enum class boundary_flag_t : std::uint16_t {
    none = 0,
    quote = 1U << 7U,
    dash = 1U << 9U,
    bracket = 1U << 10U,
    other_punctuation = 1U << 11U,
};

inline constexpr std::string_view utf8proc_version = "2.11.3";
inline constexpr std::string_view unicode_version = "17.0.0";
inline constexpr std::uint32_t punctuation_codepoint_count = 856U;

namespace compact {

[[nodiscard]] boundary_flag_t words_category(codepoint_t codepoint) noexcept;

} // namespace compact
} // namespace words::poc::unicode_backend
