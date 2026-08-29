#include "words/lexer.hpp"

#include <utf8proc.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace words {
namespace {

struct Utf8ProcDeleter final {
    void operator()(utf8proc_uint8_t *value) const noexcept {
        utf8proc_free(value);
    }
};

using Utf8Buffer = std::unique_ptr<utf8proc_uint8_t, Utf8ProcDeleter>;

struct MappedUtf8 final {
    Utf8Buffer buffer;
    std::size_t size{};
};

struct Glyph final {
    char base{};
    VowelQuantity quantity{VowelQuantity::unknown};
};

[[nodiscard]] bool is_vowel(const char value) noexcept {
    switch (value) {
    case 'a':
    case 'e':
    case 'i':
    case 'o':
    case 'u':
    case 'y':
        return true;
    default:
        return false;
    }
}

[[nodiscard]] char lookup_letter(const char value) noexcept {
    if (value == 'j') {
        return 'i';
    }
    if (value == 'v') {
        return 'u';
    }
    return value;
}

[[nodiscard]] std::expected<MappedUtf8, LexError>
map_utf8(const std::string_view input, const utf8proc_option_t options) {
    if (input.size() > static_cast<std::size_t>(
                           std::numeric_limits<utf8proc_ssize_t>::max())) {
        return std::unexpected(
            LexError{"input-too-large", "UTF-8 input exceeds utf8proc limits"});
    }

    Utf8Buffer output;
    const auto length =
        utf8proc_map(reinterpret_cast<const utf8proc_uint8_t *>(input.data()),
                     static_cast<utf8proc_ssize_t>(input.size()),
                     std::out_ptr(output), options);
    if (length < 0) {
        return std::unexpected(
            LexError{"invalid-utf8", utf8proc_errmsg(length)});
    }
    return MappedUtf8{std::move(output), static_cast<std::size_t>(length)};
}

} // namespace

std::expected<SurfaceForm, LexError>
LatinLexer::lex(const std::string_view utf8) const {
    const auto options = static_cast<utf8proc_option_t>(
        UTF8PROC_STABLE | UTF8PROC_DECOMPOSE | UTF8PROC_CASEFOLD);
    auto decomposed = map_utf8(utf8, options);
    if (!decomposed) {
        return std::unexpected(std::move(decomposed.error()));
    }

    std::vector<Glyph> glyphs;
    auto remaining = std::span<const utf8proc_uint8_t>{decomposed->buffer.get(),
                                                       decomposed->size};
    while (!remaining.empty()) {
        utf8proc_int32_t codepoint{};
        const auto consumed = utf8proc_iterate(
            remaining.data(), static_cast<utf8proc_ssize_t>(remaining.size()),
            &codepoint);
        if (consumed <= 0) {
            return std::unexpected(LexError{
                "invalid-utf8", "utf8proc produced an invalid sequence"});
        }
        remaining = remaining.subspan(static_cast<std::size_t>(consumed));

        if (codepoint >= 'a' && codepoint <= 'z') {
            glyphs.push_back(Glyph{static_cast<char>(codepoint)});
            continue;
        }
        if (codepoint == 0x0304 || codepoint == 0x0306) {
            if (glyphs.empty() || !is_vowel(glyphs.back().base) ||
                glyphs.back().quantity != VowelQuantity::unknown) {
                return std::unexpected(LexError{"invalid-vowel-quantity",
                                                "macron or breve is misplaced, "
                                                "duplicated, or conflicting"});
            }
            glyphs.back().quantity = codepoint == 0x0304
                                         ? VowelQuantity::long_vowel
                                         : VowelQuantity::short_vowel;
            continue;
        }
        return std::unexpected(LexError{"unsupported-character",
                                        "input must contain one Latin word "
                                        "with optional macrons or breves"});
    }

    if (glyphs.empty()) {
        return std::unexpected(LexError{"empty-input", "Latin word is empty"});
    }

    SurfaceForm result;
    result.original_utf8.assign(utf8);
    result.orthography_ascii.reserve(glyphs.size());
    result.lookup_ascii.reserve(glyphs.size());
    result.quantities.reserve(glyphs.size());
    result.nfc_byte_offsets.reserve(glyphs.size() + 1U);
    result.nfc_byte_offsets.push_back(0);

    for (const auto glyph : glyphs) {
        result.orthography_ascii.push_back(glyph.base);
        result.lookup_ascii.push_back(lookup_letter(glyph.base));
        result.quantities.push_back(glyph.quantity);

        std::string unit;
        unit.push_back(glyph.base);
        if (glyph.quantity != VowelQuantity::unknown) {
            std::array<utf8proc_uint8_t, 4> encoded{};
            const auto mark = glyph.quantity == VowelQuantity::long_vowel
                                  ? static_cast<utf8proc_int32_t>(0x0304)
                                  : static_cast<utf8proc_int32_t>(0x0306);
            const auto encoded_size =
                utf8proc_encode_char(mark, encoded.data());
            if (encoded_size <= 0) {
                return std::unexpected(LexError{
                    "unicode-normalization-failed",
                    "utf8proc could not encode a vowel quantity mark"});
            }
            unit.append(reinterpret_cast<const char *>(encoded.data()),
                        static_cast<std::size_t>(encoded_size));
        }
        auto composed = map_utf8(unit, static_cast<utf8proc_option_t>(
                                           UTF8PROC_STABLE | UTF8PROC_COMPOSE));
        if (!composed) {
            return std::unexpected(std::move(composed.error()));
        }
        result.normalized_nfc.append(
            reinterpret_cast<const char *>(composed->buffer.get()),
            composed->size);
        if (result.normalized_nfc.size() >
            static_cast<std::size_t>(
                std::numeric_limits<std::uint32_t>::max())) {
            return std::unexpected(LexError{
                "input-too-large", "normalized input exceeds range limits"});
        }
        result.nfc_byte_offsets.push_back(
            static_cast<std::uint32_t>(result.normalized_nfc.size()));
    }
    return result;
}

} // namespace words
