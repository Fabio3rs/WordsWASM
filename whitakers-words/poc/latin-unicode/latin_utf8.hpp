#pragma once

#include <array>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace words::poc::latin_unicode {

enum class Utf8DecodeError : std::uint8_t {
    unexpected_continuation,
    invalid_lead,
    invalid_continuation,
    truncated,
    overlong,
    surrogate,
    out_of_range,
};

struct Utf8DecodeFailure final {
    Utf8DecodeError code{Utf8DecodeError::invalid_lead};
    std::size_t byte_offset{};
    auto operator<=>(const Utf8DecodeFailure &) const = default;
};

struct DecodedScalar final {
    char32_t value{};
    std::uint8_t byte_count{};
    auto operator<=>(const DecodedScalar &) const = default;
};

enum class Utf8EncodeError : std::uint8_t {
    surrogate,
    out_of_range,
};

struct EncodedScalar final {
    std::array<char, 4> bytes{};
    std::uint8_t byte_count{};
    auto operator<=>(const EncodedScalar &) const = default;
};

// Strict scalar decoding is kept separate from the Latin allowlist. A valid
// encoding of U+FFFD is a decoded scalar; malformed input is an error.
[[nodiscard]] std::expected<DecodedScalar, Utf8DecodeFailure>
decode_utf8_scalar(std::string_view input,
                   std::size_t byte_offset = 0U) noexcept;

[[nodiscard]] constexpr std::expected<EncodedScalar, Utf8EncodeError>
encode_utf8_scalar(char32_t value) noexcept;

enum class LatinQuantity : std::uint8_t {
    unknown = 0,
    short_vowel = 1,
    long_vowel = 2,
};

struct LatinGlyph final {
    char base{};
    LatinQuantity quantity{LatinQuantity::unknown};
    auto operator<=>(const LatinGlyph &) const = default;
};

enum class LatinCodepointKind : std::uint8_t {
    base_letter,
    quantity_mark,
};

struct LatinCodepointMapping final {
    LatinCodepointKind kind{LatinCodepointKind::base_letter};
    LatinGlyph glyph{};
    auto operator<=>(const LatinCodepointMapping &) const = default;
};

// Returns a mapping only for the finite input alphabet accepted by the
// WordsWASM Latin lexer. This is not a general Unicode mapping API.
[[nodiscard]] constexpr std::optional<LatinCodepointMapping>
map_latin_codepoint(char32_t value) noexcept;

// Encodes the lowercase NFC spelling for one already validated Latin glyph.
// The only deliberately uncomposed result is y + U+0306.
[[nodiscard]] constexpr std::optional<std::array<EncodedScalar, 2>>
encode_latin_glyph_nfc(LatinGlyph glyph) noexcept;

enum class LatinSurfaceErrorCode : std::uint8_t {
    empty_input,
    input_too_large,
    invalid_utf8,
    invalid_vowel_quantity,
    unsupported_character,
    insufficient_output_space,
};

struct LatinSurfaceError final {
    LatinSurfaceErrorCode code{LatinSurfaceErrorCode::invalid_utf8};
    std::size_t byte_offset{};
    std::optional<Utf8DecodeError> utf8_error;
    std::string_view message;
    auto operator<=>(const LatinSurfaceError &) const = default;
};

struct LatinSurface final {
    std::string original_utf8;
    std::string normalized_nfc;
    std::string orthography_ascii;
    std::string lookup_ascii;
    std::vector<LatinQuantity> quantities;
    std::vector<std::uint32_t> nfc_byte_offsets;

    [[nodiscard]] std::string_view slice(std::size_t begin,
                                         std::size_t count) const noexcept;
    auto operator<=>(const LatinSurface &) const = default;
};

struct LatinSurfaceRequirements final {
    std::size_t logical_letters{};
    std::size_t normalized_nfc_bytes{};
    bool ascii_only{};
    auto operator<=>(const LatinSurfaceRequirements &) const = default;
};

struct LatinSurfaceBuffers final {
    std::span<char> normalized_nfc;
    std::span<char> orthography_ascii;
    std::span<char> lookup_ascii;
    std::span<LatinQuantity> quantities;
    std::span<std::uint32_t> nfc_byte_offsets;
};

// Non-owning result produced in caller-provided storage. The input and every
// mutually non-overlapping buffer must outlive this view; output buffers must
// not overlap the input. This path performs no dynamic allocation and checks
// the complete input and all capacities before writing any output.
struct LatinSurfaceView final {
    std::string_view original_utf8;
    std::string_view normalized_nfc;
    std::string_view orthography_ascii;
    std::string_view lookup_ascii;
    std::span<const LatinQuantity> quantities;
    std::span<const std::uint32_t> nfc_byte_offsets;

    [[nodiscard]] std::string_view slice(std::size_t begin,
                                         std::size_t count) const noexcept;
};

class LatinSurfaceNormalizer final {
  public:
    [[nodiscard]] std::expected<LatinSurfaceRequirements, LatinSurfaceError>
    requirements(std::string_view input) const;

    [[nodiscard]] std::expected<LatinSurfaceView, LatinSurfaceError>
    normalize_into(std::string_view input, LatinSurfaceBuffers buffers) const;

    [[nodiscard]] std::expected<LatinSurface, LatinSurfaceError>
    normalize(std::string_view input) const;
};

namespace detail {

inline constexpr char32_t combining_macron = 0x0304;
inline constexpr char32_t combining_breve = 0x0306;

struct SpecialLatinMapping final {
    char32_t input{};
    char base{};
    LatinQuantity quantity{LatinQuantity::unknown};
    char32_t normalized_nfc{};
    auto operator<=>(const SpecialLatinMapping &) const = default;
};

inline constexpr std::array<SpecialLatinMapping, 22> special_latin_mappings{{
    {0x0100, 'a', LatinQuantity::long_vowel, 0x0101},
    {0x0101, 'a', LatinQuantity::long_vowel, 0x0101},
    {0x0102, 'a', LatinQuantity::short_vowel, 0x0103},
    {0x0103, 'a', LatinQuantity::short_vowel, 0x0103},
    {0x0112, 'e', LatinQuantity::long_vowel, 0x0113},
    {0x0113, 'e', LatinQuantity::long_vowel, 0x0113},
    {0x0114, 'e', LatinQuantity::short_vowel, 0x0115},
    {0x0115, 'e', LatinQuantity::short_vowel, 0x0115},
    {0x012A, 'i', LatinQuantity::long_vowel, 0x012B},
    {0x012B, 'i', LatinQuantity::long_vowel, 0x012B},
    {0x012C, 'i', LatinQuantity::short_vowel, 0x012D},
    {0x012D, 'i', LatinQuantity::short_vowel, 0x012D},
    {0x014C, 'o', LatinQuantity::long_vowel, 0x014D},
    {0x014D, 'o', LatinQuantity::long_vowel, 0x014D},
    {0x014E, 'o', LatinQuantity::short_vowel, 0x014F},
    {0x014F, 'o', LatinQuantity::short_vowel, 0x014F},
    {0x016A, 'u', LatinQuantity::long_vowel, 0x016B},
    {0x016B, 'u', LatinQuantity::long_vowel, 0x016B},
    {0x016C, 'u', LatinQuantity::short_vowel, 0x016D},
    {0x016D, 'u', LatinQuantity::short_vowel, 0x016D},
    {0x0232, 'y', LatinQuantity::long_vowel, 0x0233},
    {0x0233, 'y', LatinQuantity::long_vowel, 0x0233},
}};

[[nodiscard]] constexpr bool is_ascii_letter(const char value) noexcept {
    return (value >= 'A' && value <= 'Z') || (value >= 'a' && value <= 'z');
}

[[nodiscard]] constexpr char lowercase_ascii(const char value) noexcept {
    return value >= 'A' && value <= 'Z' ? static_cast<char>(value + ('a' - 'A'))
                                        : value;
}

[[nodiscard]] constexpr bool is_vowel(const char value) noexcept {
    return value == 'a' || value == 'e' || value == 'i' || value == 'o' ||
           value == 'u' || value == 'y';
}

[[nodiscard]] constexpr char lookup_letter(const char value) noexcept {
    return value == 'j' ? 'i' : (value == 'v' ? 'u' : value);
}

[[nodiscard]] constexpr bool mappings_are_valid() {
    if (special_latin_mappings.size() != 22U) {
        return false;
    }
    for (std::size_t index = 0; index < special_latin_mappings.size();
         ++index) {
        const auto &entry = special_latin_mappings[index];
        if (!is_vowel(entry.base) || entry.quantity == LatinQuantity::unknown ||
            entry.normalized_nfc == 0U) {
            return false;
        }
        if (index != 0U &&
            special_latin_mappings[index - 1U].input >= entry.input) {
            return false;
        }
        const auto paired_index = index % 2U == 0U ? index + 1U : index - 1U;
        const auto &paired = special_latin_mappings[paired_index];
        if (entry.base != paired.base || entry.quantity != paired.quantity ||
            entry.normalized_nfc != paired.normalized_nfc) {
            return false;
        }
        if (index % 2U == 0U && (paired.input != entry.input + 1U ||
                                 entry.normalized_nfc != paired.input)) {
            return false;
        }
        for (std::size_t other = index + 1U;
             other < special_latin_mappings.size(); ++other) {
            const auto &candidate = special_latin_mappings[other];
            if (entry.input == candidate.input) {
                return false;
            }
            // Exactly the uppercase/lowercase pair may share one semantic
            // composition key.
            if (entry.base == candidate.base &&
                entry.quantity == candidate.quantity && other != paired_index) {
                return false;
            }
        }
    }

    constexpr std::array<char, 6> bases{'a', 'e', 'i', 'o', 'u', 'y'};
    for (const auto base : bases) {
        std::size_t long_count{};
        std::size_t short_count{};
        for (const auto &entry : special_latin_mappings) {
            if (entry.base != base) {
                continue;
            }
            long_count += entry.quantity == LatinQuantity::long_vowel ? 1U : 0U;
            short_count +=
                entry.quantity == LatinQuantity::short_vowel ? 1U : 0U;
        }
        if (long_count != 2U || short_count != (base == 'y' ? 0U : 2U)) {
            return false;
        }
    }
    return true;
}

static_assert(mappings_are_valid());

} // namespace detail

constexpr std::expected<EncodedScalar, Utf8EncodeError>
encode_utf8_scalar(const char32_t value) noexcept {
    EncodedScalar result;
    if (value <= 0x7F) {
        result.bytes[0] = static_cast<char>(value);
        result.byte_count = 1U;
        return result;
    }
    if (value <= 0x07FF) {
        result.bytes[0] = static_cast<char>(0xC0U | (value >> 6U));
        result.bytes[1] = static_cast<char>(0x80U | (value & 0x3FU));
        result.byte_count = 2U;
        return result;
    }
    if (value >= 0xD800 && value <= 0xDFFF) {
        return std::unexpected(Utf8EncodeError::surrogate);
    }
    if (value <= 0xFFFF) {
        result.bytes[0] = static_cast<char>(0xE0U | (value >> 12U));
        result.bytes[1] = static_cast<char>(0x80U | ((value >> 6U) & 0x3FU));
        result.bytes[2] = static_cast<char>(0x80U | (value & 0x3FU));
        result.byte_count = 3U;
        return result;
    }
    if (value > 0x10FFFF) {
        return std::unexpected(Utf8EncodeError::out_of_range);
    }
    result.bytes[0] = static_cast<char>(0xF0U | (value >> 18U));
    result.bytes[1] = static_cast<char>(0x80U | ((value >> 12U) & 0x3FU));
    result.bytes[2] = static_cast<char>(0x80U | ((value >> 6U) & 0x3FU));
    result.bytes[3] = static_cast<char>(0x80U | (value & 0x3FU));
    result.byte_count = 4U;
    return result;
}

constexpr std::optional<LatinCodepointMapping>
map_latin_codepoint(const char32_t value) noexcept {
    if ((value >= U'A' && value <= U'Z') || (value >= U'a' && value <= U'z')) {
        const auto ascii = static_cast<char>(value);
        return LatinCodepointMapping{
            .kind = LatinCodepointKind::base_letter,
            .glyph = LatinGlyph{.base = detail::lowercase_ascii(ascii)},
        };
    }
    if (value == detail::combining_macron || value == detail::combining_breve) {
        return LatinCodepointMapping{
            .kind = LatinCodepointKind::quantity_mark,
            .glyph =
                LatinGlyph{
                    .quantity = value == detail::combining_macron
                                    ? LatinQuantity::long_vowel
                                    : LatinQuantity::short_vowel,
                },
        };
    }

    std::size_t first{};
    auto count = detail::special_latin_mappings.size();
    while (count != 0U) {
        const auto step = count / 2U;
        const auto middle = first + step;
        if (detail::special_latin_mappings[middle].input < value) {
            first = middle + 1U;
            count -= step + 1U;
        } else {
            count = step;
        }
    }
    if (first == detail::special_latin_mappings.size() ||
        detail::special_latin_mappings[first].input != value) {
        return std::nullopt;
    }
    const auto &entry = detail::special_latin_mappings[first];
    return LatinCodepointMapping{
        .kind = LatinCodepointKind::base_letter,
        .glyph = LatinGlyph{.base = entry.base, .quantity = entry.quantity},
    };
}

constexpr std::optional<std::array<EncodedScalar, 2>>
encode_latin_glyph_nfc(const LatinGlyph glyph) noexcept {
    if ((glyph.base < 'a' || glyph.base > 'z') ||
        (glyph.quantity != LatinQuantity::unknown &&
         !detail::is_vowel(glyph.base))) {
        return std::nullopt;
    }

    std::array<EncodedScalar, 2> result{};
    if (glyph.quantity == LatinQuantity::unknown) {
        result[0] = *encode_utf8_scalar(static_cast<char32_t>(glyph.base));
        return result;
    }

    for (const auto &entry : detail::special_latin_mappings) {
        if (entry.base == glyph.base && entry.quantity == glyph.quantity) {
            result[0] = *encode_utf8_scalar(entry.normalized_nfc);
            return result;
        }
    }

    // The finite table has no precomposed breve-y.
    if (glyph.base == 'y' && glyph.quantity == LatinQuantity::short_vowel) {
        result[0] = *encode_utf8_scalar(U'y');
        result[1] = *encode_utf8_scalar(detail::combining_breve);
        return result;
    }
    return std::nullopt;
}

static_assert(map_latin_codepoint(U'A')->glyph.base == 'a');
static_assert(map_latin_codepoint(0x0232)->glyph ==
              LatinGlyph{'y', LatinQuantity::long_vowel});
static_assert(!map_latin_codepoint(0x212A).has_value());
static_assert(encode_latin_glyph_nfc(LatinGlyph{'y',
                                                LatinQuantity::short_vowel})
                  ->at(1)
                  .byte_count == 2U);

[[nodiscard]] constexpr bool compositions_are_valid() {
    for (std::size_t index = 0U; index < detail::special_latin_mappings.size();
         index += 2U) {
        const auto &entry = detail::special_latin_mappings[index];
        const auto encoded = encode_latin_glyph_nfc(
            LatinGlyph{.base = entry.base, .quantity = entry.quantity});
        if (!encoded ||
            encoded->at(0) != *encode_utf8_scalar(entry.normalized_nfc) ||
            encoded->at(1).byte_count != 0U) {
            return false;
        }
    }

    const auto breve_y = encode_latin_glyph_nfc(
        LatinGlyph{.base = 'y', .quantity = LatinQuantity::short_vowel});
    return breve_y && breve_y->at(0) == *encode_utf8_scalar(U'y') &&
           breve_y->at(1) == *encode_utf8_scalar(detail::combining_breve) &&
           !encode_latin_glyph_nfc(
                LatinGlyph{.base = 'b', .quantity = LatinQuantity::long_vowel})
                .has_value();
}

static_assert(compositions_are_valid());

} // namespace words::poc::latin_unicode
