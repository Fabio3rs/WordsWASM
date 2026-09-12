#pragma once

namespace words::poc::unicode_backend::compact {

#if defined(__GNUC__) || defined(__clang__)
#define WORDS_POC_ALWAYS_INLINE __attribute__((always_inline))
#else
#define WORDS_POC_ALWAYS_INLINE
#endif

WORDS_POC_ALWAYS_INLINE constexpr ssize_t
iterate(const byte_t *const input, const ssize_t input_size,
        codepoint_t *const codepoint) noexcept {
    if (codepoint == nullptr) {
        return invalid_utf8;
    }
    *codepoint = -1;
    if (input_size == 0) {
        return 0;
    }
    if (input == nullptr || input_size < 0) {
        return invalid_utf8;
    }

    const auto continuation = [](const byte_t value) constexpr noexcept {
        return (value & 0xC0U) == 0x80U;
    };
    const auto available = static_cast<std::size_t>(input_size);
    const auto lead = input[0];
    if (lead < 0x80U) {
        *codepoint = static_cast<codepoint_t>(lead);
        return 1;
    }
    if (lead < 0xC2U || lead > 0xF4U) {
        return invalid_utf8;
    }
    if (lead < 0xE0U) {
        if (available < 2U || !continuation(input[1])) {
            return invalid_utf8;
        }
        *codepoint = static_cast<codepoint_t>(
            (static_cast<std::uint32_t>(lead & 0x1FU) << 6U) |
            static_cast<std::uint32_t>(input[1] & 0x3FU));
        return 2;
    }
    if (lead < 0xF0U) {
        if (available < 3U || !continuation(input[1]) ||
            !continuation(input[2])) {
            return invalid_utf8;
        }
        if ((lead == 0xE0U && input[1] < 0xA0U) ||
            (lead == 0xEDU && input[1] > 0x9FU)) {
            return invalid_utf8;
        }
        *codepoint = static_cast<codepoint_t>(
            (static_cast<std::uint32_t>(lead & 0x0FU) << 12U) |
            (static_cast<std::uint32_t>(input[1] & 0x3FU) << 6U) |
            static_cast<std::uint32_t>(input[2] & 0x3FU));
        return 3;
    }
    if (available < 4U || !continuation(input[1]) || !continuation(input[2]) ||
        !continuation(input[3])) {
        return invalid_utf8;
    }
    if ((lead == 0xF0U && input[1] < 0x90U) ||
        (lead == 0xF4U && input[1] > 0x8FU)) {
        return invalid_utf8;
    }
    *codepoint = static_cast<codepoint_t>(
        (static_cast<std::uint32_t>(lead & 0x07U) << 18U) |
        (static_cast<std::uint32_t>(input[1] & 0x3FU) << 12U) |
        (static_cast<std::uint32_t>(input[2] & 0x3FU) << 6U) |
        static_cast<std::uint32_t>(input[3] & 0x3FU));
    return 4;
}

#undef WORDS_POC_ALWAYS_INLINE

} // namespace words::poc::unicode_backend::compact
