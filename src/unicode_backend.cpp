#include "unicode_backend.hpp"

#if defined(WORDS_UNICODE_BACKEND_COMPACT)
#include "whitakers-words/poc/latin-unicode/latin_utf8.hpp"
#endif

#include <string>
#include <utility>

namespace words::detail::unicode_backend {
namespace {

#if defined(WORDS_UNICODE_BACKEND_COMPACT)
using poc::latin_unicode::LatinQuantity;
using poc::latin_unicode::LatinSurfaceErrorCode;

[[nodiscard]] constexpr DiagnosticCode
diagnostic_code(const LatinSurfaceErrorCode code) noexcept {
    if (code == LatinSurfaceErrorCode::empty_input) {
        return DiagnosticCode::empty_input;
    }
    if (code == LatinSurfaceErrorCode::input_too_large) {
        return DiagnosticCode::input_too_large;
    }
    if (code == LatinSurfaceErrorCode::invalid_utf8) {
        return DiagnosticCode::invalid_utf8;
    }
    if (code == LatinSurfaceErrorCode::invalid_vowel_quantity) {
        return DiagnosticCode::invalid_vowel_quantity;
    }
    if (code == LatinSurfaceErrorCode::unsupported_character) {
        return DiagnosticCode::unsupported_character;
    }
    // normalize() owns its buffers, so insufficient_output_space (or an
    // unknown future value) is defensive only.
    return DiagnosticCode::unicode_normalization_failed;
}

[[nodiscard]] constexpr VowelQuantity
quantity(const LatinQuantity value) noexcept {
    if (value == LatinQuantity::short_vowel) {
        return VowelQuantity::short_vowel;
    }
    if (value == LatinQuantity::long_vowel) {
        return VowelQuantity::long_vowel;
    }
    return VowelQuantity::unknown;
}
#endif

} // namespace

BoundaryFlag words_category(const codepoint_t codepoint) noexcept {
#if defined(WORDS_UNICODE_BACKEND_COMPACT)
    const auto result =
        poc::unicode_backend::compact::words_category(codepoint);
    static_assert(
        std::to_underlying(BoundaryFlag::none) ==
        std::to_underlying(poc::unicode_backend::boundary_flag_t::none));
    static_assert(
        std::to_underlying(BoundaryFlag::quote) ==
        std::to_underlying(poc::unicode_backend::boundary_flag_t::quote));
    static_assert(
        std::to_underlying(BoundaryFlag::dash) ==
        std::to_underlying(poc::unicode_backend::boundary_flag_t::dash));
    static_assert(
        std::to_underlying(BoundaryFlag::bracket) ==
        std::to_underlying(poc::unicode_backend::boundary_flag_t::bracket));
    static_assert(
        std::to_underlying(BoundaryFlag::other_punctuation) ==
        std::to_underlying(
            poc::unicode_backend::boundary_flag_t::other_punctuation));
    return static_cast<BoundaryFlag>(std::to_underlying(result));
#else
    const auto category = utf8proc_category(codepoint);
    if (category == UTF8PROC_CATEGORY_PI || category == UTF8PROC_CATEGORY_PF) {
        return BoundaryFlag::quote;
    }
    if (category == UTF8PROC_CATEGORY_PD) {
        return BoundaryFlag::dash;
    }
    if (category == UTF8PROC_CATEGORY_PS || category == UTF8PROC_CATEGORY_PE) {
        return BoundaryFlag::bracket;
    }
    if (category >= UTF8PROC_CATEGORY_PC && category <= UTF8PROC_CATEGORY_PO) {
        return BoundaryFlag::other_punctuation;
    }
    return BoundaryFlag::none;
#endif
}

#if defined(WORDS_UNICODE_BACKEND_COMPACT)
std::expected<SurfaceForm, LexError>
normalize_latin(const std::string_view input) {
    auto compact =
        poc::latin_unicode::LatinSurfaceNormalizer{}.normalize(input);
    if (!compact) {
        return std::unexpected(
            LexError{.code = diagnostic_code(compact.error().code),
                     .message = std::string{compact.error().message}});
    }

    SurfaceForm result{
        .original_utf8 = std::move(compact->original_utf8),
        .normalized_nfc = std::move(compact->normalized_nfc),
        .orthography_ascii = std::move(compact->orthography_ascii),
        .lookup_ascii = std::move(compact->lookup_ascii),
        .quantities = {},
        .nfc_byte_offsets = std::move(compact->nfc_byte_offsets),
    };
    result.quantities.reserve(compact->quantities.size());
    for (const auto value : compact->quantities) {
        result.quantities.push_back(quantity(value));
    }
    return result;
}
#endif

} // namespace words::detail::unicode_backend
