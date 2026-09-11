#include "latin_utf8.hpp"

#include "words/lexer.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <string_view>
#include <type_traits>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t *data,
                                      std::size_t size);

namespace {

constexpr std::size_t maximum_fuzz_input_size = 64U;

[[nodiscard]] bool
equivalent(const words::poc::latin_unicode::LatinSurface &candidate,
           const words::SurfaceForm &oracle) {
    if (candidate.original_utf8 != oracle.original_utf8 ||
        candidate.normalized_nfc != oracle.normalized_nfc ||
        candidate.orthography_ascii != oracle.orthography_ascii ||
        candidate.lookup_ascii != oracle.lookup_ascii ||
        candidate.nfc_byte_offsets != oracle.nfc_byte_offsets ||
        candidate.quantities.size() != oracle.quantities.size()) {
        return false;
    }
    for (std::size_t index = 0U; index < candidate.quantities.size(); ++index) {
        if (std::to_underlying(candidate.quantities[index]) !=
            std::to_underlying(oracle.quantities[index])) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool
equivalent(const words::poc::latin_unicode::LatinSurfaceView &candidate,
           const words::SurfaceForm &oracle) {
    if (candidate.original_utf8 != oracle.original_utf8 ||
        candidate.normalized_nfc != oracle.normalized_nfc ||
        candidate.orthography_ascii != oracle.orthography_ascii ||
        candidate.lookup_ascii != oracle.lookup_ascii ||
        !std::ranges::equal(candidate.nfc_byte_offsets,
                            oracle.nfc_byte_offsets) ||
        candidate.quantities.size() != oracle.quantities.size()) {
        return false;
    }
    for (std::size_t index = 0U; index < candidate.quantities.size(); ++index) {
        if (std::to_underlying(candidate.quantities[index]) !=
            std::to_underlying(oracle.quantities[index])) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] std::uint8_t oracle_error(const words::DiagnosticCode code) {
    using words::DiagnosticCode;
    using words::poc::latin_unicode::LatinSurfaceErrorCode;
    if (code == DiagnosticCode::empty_input) {
        return std::to_underlying(LatinSurfaceErrorCode::empty_input);
    }
    if (code == DiagnosticCode::input_too_large) {
        return std::to_underlying(LatinSurfaceErrorCode::input_too_large);
    }
    if (code == DiagnosticCode::invalid_utf8) {
        return std::to_underlying(LatinSurfaceErrorCode::invalid_utf8);
    }
    if (code == DiagnosticCode::invalid_vowel_quantity) {
        return std::to_underlying(
            LatinSurfaceErrorCode::invalid_vowel_quantity);
    }
    if (code == DiagnosticCode::unsupported_character) {
        return std::to_underlying(LatinSurfaceErrorCode::unsupported_character);
    }
    return std::numeric_limits<std::uint8_t>::max();
}

} // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t *data,
                                      const std::size_t size) {
    if (size > maximum_fuzz_input_size) {
        return 0;
    }
    // LLVM's byte-oriented C ABI necessarily arrives as uint8_t storage.
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    const auto input =
        std::string_view{reinterpret_cast<const char *>(data), size};
    const words::poc::latin_unicode::LatinSurfaceNormalizer normalizer;
    const auto candidate = normalizer.normalize(input);
    const auto required = normalizer.requirements(input);
    const auto oracle = words::LatinLexer{}.lex(input);
    if (candidate.has_value() != oracle.has_value() ||
        required.has_value() != candidate.has_value()) {
        std::abort();
    }
    if (candidate && oracle) {
        if (!equivalent(*candidate, *oracle)) {
            std::abort();
        }
        std::array<char, maximum_fuzz_input_size> normalized;
        std::array<char, maximum_fuzz_input_size> orthography;
        std::array<char, maximum_fuzz_input_size> lookup;
        std::array<words::poc::latin_unicode::LatinQuantity,
                   maximum_fuzz_input_size>
            quantities;
        std::array<std::uint32_t, maximum_fuzz_input_size + 1U> offsets;
        const auto view = normalizer.normalize_into(
            input, words::poc::latin_unicode::LatinSurfaceBuffers{
                       .normalized_nfc = normalized,
                       .orthography_ascii = orthography,
                       .lookup_ascii = lookup,
                       .quantities = quantities,
                       .nfc_byte_offsets = offsets,
                   });
        if (!view || !equivalent(*view, *oracle)) {
            std::abort();
        }
    } else if (std::to_underlying(candidate.error().code) !=
                   oracle_error(oracle.error().code) ||
               required.error().code != candidate.error().code) {
        std::abort();
    }
    return 0;
}
