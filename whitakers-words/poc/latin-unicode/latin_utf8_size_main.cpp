#include "latin_utf8.hpp"

#if defined(LATIN_UNICODE_SIZE_UTF8PROC)
#include <utf8proc.h>
#endif

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

int main(const int argc, const char *const argv[]) {
    const std::string_view input =
        argc >= 2 ? argv[1] : "J\xC5\xAAV\xC4\x94NIS";
#if defined(LATIN_UNICODE_SIZE_UTF8PROC)
    utf8proc_uint8_t *decomposed{};
    const auto decompose_options = static_cast<utf8proc_option_t>(
        UTF8PROC_STABLE | UTF8PROC_DECOMPOSE | UTF8PROC_CASEFOLD);
    const auto decomposed_size =
        utf8proc_map(reinterpret_cast<const utf8proc_uint8_t *>(input.data()),
                     static_cast<utf8proc_ssize_t>(input.size()), &decomposed,
                     decompose_options);
    if (decomposed_size < 0) {
        return 1;
    }
    utf8proc_uint8_t *composed{};
    const auto compose_options =
        static_cast<utf8proc_option_t>(UTF8PROC_STABLE | UTF8PROC_COMPOSE);
    const auto composed_size =
        utf8proc_map(decomposed, decomposed_size, &composed, compose_options);
    utf8proc_free(decomposed);
    if (composed_size < 0) {
        return 1;
    }
    const auto empty = composed_size == 0;
    utf8proc_free(composed);
    if (empty) {
        return 1;
    }
#elif defined(LATIN_UNICODE_SIZE_INTO)
    constexpr std::size_t capacity = 64U;
    if (input.size() > capacity) {
        return 1;
    }
    std::array<char, capacity> normalized;
    std::array<char, capacity> orthography;
    std::array<char, capacity> lookup;
    std::array<words::poc::latin_unicode::LatinQuantity, capacity> quantities;
    std::array<std::uint32_t, capacity + 1U> offsets;
    const auto surface =
        words::poc::latin_unicode::LatinSurfaceNormalizer{}.normalize_into(
            input, words::poc::latin_unicode::LatinSurfaceBuffers{
                       .normalized_nfc = normalized,
                       .orthography_ascii = orthography,
                       .lookup_ascii = lookup,
                       .quantities = quantities,
                       .nfc_byte_offsets = offsets,
                   });
    if (!surface || surface->normalized_nfc.empty()) {
        return 1;
    }
#else
    const auto surface =
        words::poc::latin_unicode::LatinSurfaceNormalizer{}.normalize(input);
    if (!surface) {
        return 1;
    }
    if (surface->normalized_nfc.empty()) {
        return 1;
    }
#endif
    return 0;
}
