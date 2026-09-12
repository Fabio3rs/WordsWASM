#include "unicode_backend_selected.hpp"

#if defined(WORDS_POC_BACKEND_COMPACT)
#include "latin_utf8.hpp"
#else
#include <utf8proc.h>
#endif

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <utility>

namespace backend = words::poc::unicode_backend;

int main(const int argc, const char *const argv[]) {
    const std::string_view input =
        argc >= 2 ? argv[1] : "J\xC5\xAAV\xC4\x94NIS";
    backend::codepoint_t codepoint{};
    const auto *const bytes =
        reinterpret_cast<const backend::byte_t *>(input.data());

#if defined(WORDS_POC_BACKEND_COMPACT)
    const auto consumed = backend::selected::iterate(
        bytes, static_cast<backend::ssize_t>(input.size()), &codepoint);
    const auto boundary = backend::selected::words_category(codepoint);
    const auto surface =
        words::poc::latin_unicode::LatinSurfaceNormalizer{}.normalize(input);
    if (consumed <= 0 || !surface || surface->normalized_nfc.empty()) {
        return 1;
    }
#else
    const auto consumed = backend::selected::iterate(
        bytes, static_cast<backend::ssize_t>(input.size()), &codepoint);
    const auto boundary = backend::selected::words_category(codepoint);

    utf8proc_uint8_t *decomposed{};
    const auto decompose_options = static_cast<utf8proc_option_t>(
        UTF8PROC_STABLE | UTF8PROC_DECOMPOSE | UTF8PROC_CASEFOLD);
    const auto decomposed_size =
        utf8proc_map(bytes, static_cast<utf8proc_ssize_t>(input.size()),
                     &decomposed, decompose_options);
    if (decomposed_size < 0) {
        return 1;
    }
    utf8proc_uint8_t *composed{};
    const auto compose_options =
        static_cast<utf8proc_option_t>(UTF8PROC_STABLE | UTF8PROC_COMPOSE);
    const auto composed_size =
        utf8proc_map(decomposed, decomposed_size, &composed, compose_options);
    utf8proc_free(decomposed);
    if (consumed <= 0 || composed_size <= 0) {
        utf8proc_free(composed);
        return 1;
    }
    utf8proc_free(composed);
#endif

    return static_cast<int>(std::to_underlying(boundary) & 0x7FU);
}
