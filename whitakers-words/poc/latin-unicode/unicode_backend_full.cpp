#include "unicode_backend.hpp"

#include <utf8proc.h>

#include <type_traits>

namespace words::poc::unicode_backend::full {

static_assert(std::is_same_v<byte_t, utf8proc_uint8_t>);
static_assert(std::is_same_v<codepoint_t, utf8proc_int32_t>);
static_assert(std::is_same_v<ssize_t, utf8proc_ssize_t>);
static_assert(invalid_utf8 == UTF8PROC_ERROR_INVALIDUTF8);

ssize_t iterate(const byte_t *const input, const ssize_t input_size,
                codepoint_t *const codepoint) noexcept {
    return utf8proc_iterate(input, input_size, codepoint);
}

ssize_t encode_char(const codepoint_t codepoint,
                    byte_t *const output) noexcept {
    return utf8proc_encode_char(codepoint, output);
}

boundary_flag_t words_category(const codepoint_t codepoint) noexcept {
    const auto category = utf8proc_category(codepoint);
    if (category == UTF8PROC_CATEGORY_PI || category == UTF8PROC_CATEGORY_PF) {
        return boundary_flag_t::quote;
    }
    if (category == UTF8PROC_CATEGORY_PD) {
        return boundary_flag_t::dash;
    }
    if (category == UTF8PROC_CATEGORY_PS || category == UTF8PROC_CATEGORY_PE) {
        return boundary_flag_t::bracket;
    }
    if (category >= UTF8PROC_CATEGORY_PC && category <= UTF8PROC_CATEGORY_PO) {
        return boundary_flag_t::other_punctuation;
    }
    return boundary_flag_t::none;
}

} // namespace words::poc::unicode_backend::full
