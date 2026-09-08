#include "words/lexer.hpp"

#include <utf8proc.h>

#include <algorithm>
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

constexpr utf8proc_int32_t macron = 0x0304;
constexpr utf8proc_int32_t breve = 0x0306;
// WHY: keeping the allowlist ordered lets validation remain allocation-free
// and logarithmic without accepting broader Unicode case-fold equivalences.
constexpr std::array<utf8proc_int32_t, 22> precomposed_quantity_characters{
    U'Ā', U'ā', U'Ă', U'ă', U'Ē', U'ē', U'Ĕ', U'ĕ', U'Ī', U'ī', U'Ĭ',
    U'ĭ', U'Ō', U'ō', U'Ŏ', U'ŏ', U'Ū', U'ū', U'Ŭ', U'ŭ', U'Ȳ', U'ȳ',
};

[[nodiscard]] constexpr bool
is_supported_quantity_mark(const utf8proc_int32_t codepoint) noexcept {
    return codepoint == macron || codepoint == breve;
}

[[nodiscard]] constexpr bool
is_unicode_whitespace(const utf8proc_int32_t codepoint) noexcept {
    return (codepoint >= 0x0009 && codepoint <= 0x000D) ||
           codepoint == 0x0020 || codepoint == 0x0085 || codepoint == 0x00A0 ||
           codepoint == 0x1680 ||
           (codepoint >= 0x2000 && codepoint <= 0x200A) ||
           codepoint == 0x2028 || codepoint == 0x2029 || codepoint == 0x202F ||
           codepoint == 0x205F || codepoint == 0x3000;
}

[[nodiscard]] BoundaryFlag
boundary_flag(const utf8proc_int32_t codepoint) noexcept {
    if (is_unicode_whitespace(codepoint)) {
        return BoundaryFlag::whitespace;
    }

    switch (codepoint) {
    case U',':
    case U'،':
    case U'、':
    case U'，':
        return BoundaryFlag::comma;
    case U';':
    case U'؛':
    case U'；':
        return BoundaryFlag::semicolon;
    case U':':
    case U'：':
        return BoundaryFlag::colon;
    case U'.':
    case U'․':
    case U'…':
    case U'。':
    case U'．':
        return BoundaryFlag::period;
    case U'?':
    case U'؟':
    case U'？':
        return BoundaryFlag::question;
    case U'!':
    case U'！':
        return BoundaryFlag::exclamation;
    case U'\'':
    case U'’':
        return BoundaryFlag::apostrophe;
    case U'"':
        return BoundaryFlag::quote;
    default:
        break;
    }

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
}

struct DecodedCodepoint final {
    utf8proc_int32_t value{};
    std::size_t byte_count{1U};
    bool valid{};
};

[[nodiscard]] DecodedCodepoint
decode_at(const std::span<const utf8proc_uint8_t> bytes,
          const std::size_t byte_offset) noexcept {
    const auto remaining = bytes.subspan(byte_offset);
    const auto available = static_cast<utf8proc_ssize_t>(std::min(
        remaining.size(), static_cast<std::size_t>(
                              std::numeric_limits<utf8proc_ssize_t>::max())));
    utf8proc_int32_t codepoint{};
    const auto consumed =
        utf8proc_iterate(remaining.data(), available, &codepoint);
    if (consumed <= 0) {
        return {};
    }
    return DecodedCodepoint{.value = codepoint,
                            .byte_count = static_cast<std::size_t>(consumed),
                            .valid = true};
}

[[nodiscard]] constexpr bool
is_supported_original_codepoint(const utf8proc_int32_t codepoint) noexcept {
    if ((codepoint >= 'A' && codepoint <= 'Z') ||
        (codepoint >= 'a' && codepoint <= 'z') ||
        is_supported_quantity_mark(codepoint)) {
        return true;
    }

    // WHY: validation must precede case folding.  Otherwise compatibility
    // characters and non-Latin letters such as Kelvin sign or sharp-s can
    // become ordinary ASCII and silently enter the Latin dictionary lookup.
    return std::ranges::binary_search(precomposed_quantity_characters,
                                      codepoint);
}

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
            LexError{.code = "input-too-large",
                     .message = "UTF-8 input exceeds utf8proc limits"});
    }

    Utf8Buffer output;
    const auto length =
        utf8proc_map(reinterpret_cast<const utf8proc_uint8_t *>(input.data()),
                     static_cast<utf8proc_ssize_t>(input.size()),
                     std::out_ptr(output), options);
    if (length < 0) {
        return std::unexpected(LexError{.code = "invalid-utf8",
                                        .message = utf8proc_errmsg(length)});
    }
    return MappedUtf8{.buffer = std::move(output),
                      .size = static_cast<std::size_t>(length)};
}

[[nodiscard]] std::expected<void, LexError>
validate_original_input(const std::string_view input) {
    if (input.size() > static_cast<std::size_t>(
                           std::numeric_limits<utf8proc_ssize_t>::max())) {
        return std::unexpected(
            LexError{.code = "input-too-large",
                     .message = "UTF-8 input exceeds utf8proc limits"});
    }

    auto remaining = std::span<const utf8proc_uint8_t>{
        reinterpret_cast<const utf8proc_uint8_t *>(input.data()), input.size()};
    while (!remaining.empty()) {
        utf8proc_int32_t codepoint{};
        const auto consumed = utf8proc_iterate(
            remaining.data(), static_cast<utf8proc_ssize_t>(remaining.size()),
            &codepoint);
        if (consumed <= 0) {
            return std::unexpected(LexError{
                .code = "invalid-utf8", .message = utf8proc_errmsg(consumed)});
        }
        if (!is_supported_original_codepoint(codepoint)) {
            return std::unexpected(LexError{
                .code = "unsupported-character",
                .message =
                    "input must contain ASCII Latin letters with optional "
                    "macrons or breves"});
        }
        remaining = remaining.subspan(static_cast<std::size_t>(consumed));
    }
    return {};
}

[[nodiscard]] std::expected<void, LexError>
append_codepoint(std::string &output, const utf8proc_int32_t codepoint) {
    std::array<utf8proc_uint8_t, 4> encoded{};
    const auto encoded_size = utf8proc_encode_char(codepoint, encoded.data());
    if (encoded_size <= 0) {
        return std::unexpected(LexError{
            .code = "unicode-normalization-failed",
            .message = "utf8proc could not encode a vowel quantity mark"});
    }
    output.append(reinterpret_cast<const char *>(encoded.data()),
                  static_cast<std::size_t>(encoded_size));
    return {};
}

[[nodiscard]] std::expected<void, LexError>
build_logical_offsets(SurfaceForm &surface) {
    surface.nfc_byte_offsets.clear();
    surface.nfc_byte_offsets.reserve(surface.quantities.size() + 1U);
    surface.nfc_byte_offsets.push_back(0U);

    auto remaining = std::span<const utf8proc_uint8_t>{
        reinterpret_cast<const utf8proc_uint8_t *>(
            surface.normalized_nfc.data()),
        surface.normalized_nfc.size()};
    std::size_t byte_offset{};
    bool has_logical_letter{};
    while (!remaining.empty()) {
        utf8proc_int32_t codepoint{};
        const auto consumed = utf8proc_iterate(
            remaining.data(), static_cast<utf8proc_ssize_t>(remaining.size()),
            &codepoint);
        if (consumed <= 0) {
            return std::unexpected(LexError{
                .code = "unicode-normalization-failed",
                .message = "utf8proc produced an invalid NFC sequence"});
        }
        if (!is_supported_quantity_mark(codepoint)) {
            if (has_logical_letter) {
                surface.nfc_byte_offsets.push_back(
                    static_cast<std::uint32_t>(byte_offset));
            }
            has_logical_letter = true;
        } else if (!has_logical_letter) {
            return std::unexpected(
                LexError{.code = "unicode-normalization-failed",
                         .message = "NFC quantity mark has no base letter"});
        }
        const auto consumed_size = static_cast<std::size_t>(consumed);
        byte_offset += consumed_size;
        remaining = remaining.subspan(consumed_size);
    }
    if (has_logical_letter) {
        surface.nfc_byte_offsets.push_back(
            static_cast<std::uint32_t>(byte_offset));
    }
    if (surface.nfc_byte_offsets.size() != surface.quantities.size() + 1U) {
        return std::unexpected(
            LexError{.code = "unicode-normalization-failed",
                     .message = "NFC logical-letter boundaries do not match "
                                "the Latin surface"});
    }
    return {};
}

} // namespace

std::optional<TextTokenCursor::CachedToken>
TextTokenCursor::scan(const std::size_t byte_offset) const noexcept {
    const auto bytes = std::span<const utf8proc_uint8_t>{
        reinterpret_cast<const utf8proc_uint8_t *>(input_.data()),
        input_.size()};
    auto cursor = byte_offset;

    while (cursor < bytes.size()) {
        const auto decoded = decode_at(bytes, cursor);
        if (!decoded.valid ||
            boundary_flag(decoded.value) == BoundaryFlag::none) {
            break;
        }
        cursor += decoded.byte_count;
    }
    if (cursor == bytes.size()) {
        return std::nullopt;
    }

    const auto word_begin = cursor;
    while (cursor < bytes.size()) {
        const auto decoded = decode_at(bytes, cursor);
        if (decoded.valid &&
            boundary_flag(decoded.value) != BoundaryFlag::none) {
            break;
        }
        cursor += decoded.byte_count;
    }
    const auto word_end = cursor;

    TextBoundary boundary{.byte_begin = word_end, .byte_end = word_end};
    while (cursor < bytes.size()) {
        const auto decoded = decode_at(bytes, cursor);
        if (!decoded.valid) {
            break;
        }
        const auto flag = boundary_flag(decoded.value);
        if (flag == BoundaryFlag::none) {
            break;
        }
        boundary.flags |= flag;
        cursor += decoded.byte_count;
        boundary.byte_end = cursor;
    }

    return CachedToken{
        .token =
            TextToken{
                .text = input_.substr(word_begin, word_end - word_begin),
                .byte_begin = word_begin,
                .byte_end = word_end,
                .boundary_after = boundary,
            },
        .next_cursor = cursor,
    };
}

const TextToken *TextTokenCursor::peek() noexcept {
    if (!peeked_) {
        peeked_ = scan(cursor_);
    }
    return peeked_ ? &peeked_->token : nullptr;
}

std::optional<TextToken> TextTokenCursor::next() noexcept {
    if (peek() == nullptr) {
        return std::nullopt;
    }
    auto token = peeked_->token;
    cursor_ = peeked_->next_cursor;
    peeked_.reset();
    return token;
}

std::expected<SurfaceForm, LexError>
LatinLexer::lex(const std::string_view utf8) const {
    auto validation = validate_original_input(utf8);
    if (!validation) {
        return std::unexpected(std::move(validation.error()));
    }

    // WHY: utf8proc exposes option_t as a C enum but documents it as a bitmask;
    // valid flag combinations are intentionally not individual enumerators.
    // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange)
    const auto options = static_cast<utf8proc_option_t>(
        UTF8PROC_STABLE | UTF8PROC_DECOMPOSE | UTF8PROC_CASEFOLD);
    auto decomposed = map_utf8(utf8, options);
    if (!decomposed) {
        return std::unexpected(std::move(decomposed.error()));
    }

    std::vector<Glyph> glyphs;
    // Every accepted glyph consumes at least one input byte. Reserving that
    // upper bound turns the common ASCII word from several geometric vector
    // growths into one small allocation without changing Unicode handling.
    glyphs.reserve(utf8.size());
    auto remaining = std::span<const utf8proc_uint8_t>{decomposed->buffer.get(),
                                                       decomposed->size};
    while (!remaining.empty()) {
        utf8proc_int32_t codepoint{};
        const auto consumed = utf8proc_iterate(
            remaining.data(), static_cast<utf8proc_ssize_t>(remaining.size()),
            &codepoint);
        if (consumed <= 0) {
            return std::unexpected(
                LexError{.code = "invalid-utf8",
                         .message = "utf8proc produced an invalid sequence"});
        }
        remaining = remaining.subspan(static_cast<std::size_t>(consumed));

        if (codepoint >= 'a' && codepoint <= 'z') {
            glyphs.push_back(Glyph{.base = static_cast<char>(codepoint)});
            continue;
        }
        if (is_supported_quantity_mark(codepoint)) {
            if (glyphs.empty() || !is_vowel(glyphs.back().base) ||
                glyphs.back().quantity != VowelQuantity::unknown) {
                return std::unexpected(
                    LexError{.code = "invalid-vowel-quantity",
                             .message = "macron or breve is misplaced, "
                                        "duplicated, or conflicting"});
            }
            glyphs.back().quantity = codepoint == macron
                                         ? VowelQuantity::long_vowel
                                         : VowelQuantity::short_vowel;
            continue;
        }
        return std::unexpected(LexError{.code = "unsupported-character",
                                        .message =
                                            "input must contain one Latin word "
                                            "with optional macrons or breves"});
    }

    if (glyphs.empty()) {
        return std::unexpected(
            LexError{.code = "empty-input", .message = "Latin word is empty"});
    }

    SurfaceForm result;
    result.original_utf8.assign(utf8);
    result.orthography_ascii.reserve(glyphs.size());
    result.lookup_ascii.reserve(glyphs.size());
    result.quantities.reserve(glyphs.size());
    std::string decomposed_normalized;
    decomposed_normalized.reserve(decomposed->size);

    for (const auto glyph : glyphs) {
        result.orthography_ascii.push_back(glyph.base);
        result.lookup_ascii.push_back(lookup_letter(glyph.base));
        result.quantities.push_back(glyph.quantity);
        decomposed_normalized.push_back(glyph.base);
        if (glyph.quantity != VowelQuantity::unknown) {
            const auto mark =
                glyph.quantity == VowelQuantity::long_vowel ? macron : breve;
            auto appended = append_codepoint(decomposed_normalized, mark);
            if (!appended) {
                return std::unexpected(std::move(appended.error()));
            }
        }
    }

    // WHY: composing the whole normalized surface once avoids one allocation
    // per Latin letter while still letting the following pass cache exact
    // logical-letter boundaries, including a base plus an uncomposed mark.
    // WHY: this is the same documented utf8proc bitmask convention used by
    // the decomposition pass above.
    // NOLINTBEGIN(clang-analyzer-optin.core.EnumCastOutOfRange)
    const auto compose_options =
        static_cast<utf8proc_option_t>(UTF8PROC_STABLE | UTF8PROC_COMPOSE);
    // NOLINTEND(clang-analyzer-optin.core.EnumCastOutOfRange)
    auto composed = map_utf8(decomposed_normalized, compose_options);
    if (!composed) {
        return std::unexpected(std::move(composed.error()));
    }
    result.normalized_nfc.assign(
        reinterpret_cast<const char *>(composed->buffer.get()), composed->size);

    // 2GB limit seems ok
    if (result.normalized_nfc.size() >
        static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max())) {
        return std::unexpected(
            LexError{.code = "input-too-large",
                     .message = "normalized input exceeds range limits"});
    }
    auto offsets = build_logical_offsets(result);
    if (!offsets) {
        return std::unexpected(std::move(offsets.error()));
    }
    return result;
}

} // namespace words
