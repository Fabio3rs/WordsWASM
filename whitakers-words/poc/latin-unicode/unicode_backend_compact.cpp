#include "unicode_backend.hpp"

#include "latin_utf8.hpp"

#include <cstring>

namespace words::poc::unicode_backend::compact {

ssize_t encode_char(const codepoint_t codepoint,
                    byte_t *const output) noexcept {
    if (output == nullptr || codepoint < 0) {
        return 0;
    }
    const auto encoded =
        latin_unicode::encode_utf8_scalar(static_cast<char32_t>(codepoint));
    if (!encoded) {
        return 0;
    }
    std::memcpy(output, encoded->bytes.data(), encoded->byte_count);
    return static_cast<ssize_t>(encoded->byte_count);
}

} // namespace words::poc::unicode_backend::compact
