#include "latin_utf8.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <string_view>
#include <utility>

namespace words::poc::latin_unicode {
namespace {

constexpr std::uint8_t ascii_max = 0x7FU;
constexpr std::uint8_t continuation_first = 0x80U;
constexpr std::uint8_t continuation_last = 0xBFU;
constexpr std::uint8_t continuation_mask = 0xC0U;
constexpr std::uint8_t overlong_two_first = 0xC0U;
constexpr std::uint8_t overlong_two_last = 0xC1U;
constexpr std::uint8_t two_byte_lead_last = 0xDFU;
constexpr std::uint8_t three_byte_lead_first = 0xE0U;
constexpr std::uint8_t surrogate_lead = 0xEDU;
constexpr std::uint8_t three_byte_lead_last = 0xEFU;
constexpr std::uint8_t four_byte_lead_first = 0xF0U;
constexpr std::uint8_t four_byte_lead_last = 0xF4U;
constexpr std::uint8_t out_of_range_lead_first = 0xF5U;
constexpr std::uint8_t out_of_range_lead_last = 0xF7U;
constexpr std::uint8_t invalid_lead_first = 0xF8U;
constexpr std::uint8_t three_byte_second_min = 0xA0U;
constexpr std::uint8_t surrogate_second_min = 0xA0U;
constexpr std::uint8_t four_byte_second_min = 0x90U;
constexpr std::uint8_t four_byte_second_max = 0x8FU;

[[nodiscard]] constexpr std::uint8_t
byte_at(const std::string_view input, const std::size_t offset) noexcept {
    return static_cast<std::uint8_t>(static_cast<unsigned char>(input[offset]));
}

[[nodiscard]] constexpr bool
is_continuation(const std::uint8_t value) noexcept {
    return (value & continuation_mask) == continuation_first;
}

[[nodiscard]] std::unexpected<Utf8DecodeFailure>
decode_failure(const Utf8DecodeError code, const std::size_t offset) noexcept {
    return std::unexpected(
        Utf8DecodeFailure{.code = code, .byte_offset = offset});
}

[[nodiscard]] LatinSurfaceError
surface_error(const LatinSurfaceErrorCode code, const std::size_t offset,
              const std::string_view message,
              const std::optional<Utf8DecodeError> utf8_error = std::nullopt) {
    return LatinSurfaceError{.code = code,
                             .byte_offset = offset,
                             .utf8_error = utf8_error,
                             .message = message};
}

[[nodiscard]] std::expected<void, LatinSurfaceError>
validate_finite_input(const std::string_view input) {
    std::size_t byte_offset{};
    while (byte_offset < input.size()) {
        const auto decoded = decode_utf8_scalar(input, byte_offset);
        if (!decoded) {
            return std::unexpected(surface_error(
                LatinSurfaceErrorCode::invalid_utf8,
                decoded.error().byte_offset, "input is not valid UTF-8",
                decoded.error().code));
        }
        if (!map_latin_codepoint(decoded->value)) {
            return std::unexpected(surface_error(
                LatinSurfaceErrorCode::unsupported_character, byte_offset,
                "input contains a character outside the finite Latin "
                "alphabet"));
        }
        byte_offset += decoded->byte_count;
    }
    return {};
}

[[nodiscard]] std::expected<void, LatinSurfaceError>
account_glyph(LatinSurfaceRequirements &requirements, const LatinGlyph glyph) {
    const auto encoded = encode_latin_glyph_nfc(glyph);
    if (!encoded) {
        return std::unexpected(surface_error(
            LatinSurfaceErrorCode::unsupported_character, 0U,
            "internal Latin glyph is outside the finite mapping"));
    }
    std::size_t encoded_size{};
    for (const auto &scalar : *encoded) {
        encoded_size += scalar.byte_count;
    }
    const auto maximum =
        static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max());
    if (encoded_size > maximum - requirements.normalized_nfc_bytes) {
        return std::unexpected(
            surface_error(LatinSurfaceErrorCode::input_too_large, 0U,
                          "normalized input exceeds range limits"));
    }
    requirements.normalized_nfc_bytes += encoded_size;
    ++requirements.logical_letters;
    return {};
}

[[nodiscard]] bool is_all_ascii_letters(const std::string_view input) noexcept {
    return !input.empty() &&
           std::ranges::all_of(input, detail::is_ascii_letter);
}

void append_encoded(std::string &output, const EncodedScalar &encoded) {
    output.append(encoded.bytes.data(),
                  static_cast<std::size_t>(encoded.byte_count));
}

[[nodiscard]] std::expected<void, LatinSurfaceError>
append_glyph(LatinSurface &surface, const LatinGlyph glyph) {
    const auto encoded = encode_latin_glyph_nfc(glyph);
    if (!encoded) {
        return std::unexpected(surface_error(
            LatinSurfaceErrorCode::unsupported_character, 0U,
            "internal Latin glyph is outside the finite mapping"));
    }

    surface.orthography_ascii.push_back(glyph.base);
    surface.lookup_ascii.push_back(detail::lookup_letter(glyph.base));
    surface.quantities.push_back(glyph.quantity);
    for (const auto &scalar : *encoded) {
        if (scalar.byte_count != 0U) {
            append_encoded(surface.normalized_nfc, scalar);
        }
    }
    if (surface.normalized_nfc.size() >
        static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max())) {
        return std::unexpected(
            surface_error(LatinSurfaceErrorCode::input_too_large, 0U,
                          "normalized input exceeds range limits"));
    }
    surface.nfc_byte_offsets.push_back(
        static_cast<std::uint32_t>(surface.normalized_nfc.size()));
    return {};
}

[[nodiscard]] LatinSurface normalize_ascii(const std::string_view input) {
    LatinSurface result;
    result.original_utf8.assign(input);
    result.normalized_nfc.reserve(input.size());
    result.orthography_ascii.reserve(input.size());
    result.lookup_ascii.reserve(input.size());
    result.quantities.reserve(input.size());
    result.nfc_byte_offsets.reserve(input.size() + 1U);
    result.nfc_byte_offsets.push_back(0U);

    for (const auto value : input) {
        const auto lowercase = detail::lowercase_ascii(value);
        result.normalized_nfc.push_back(lowercase);
        result.orthography_ascii.push_back(lowercase);
        result.lookup_ascii.push_back(detail::lookup_letter(lowercase));
        result.quantities.push_back(LatinQuantity::unknown);
        result.nfc_byte_offsets.push_back(
            static_cast<std::uint32_t>(result.normalized_nfc.size()));
    }
    return result;
}

} // namespace

std::expected<DecodedScalar, Utf8DecodeFailure>
decode_utf8_scalar(const std::string_view input,
                   const std::size_t byte_offset) noexcept {
    if (byte_offset >= input.size()) {
        return decode_failure(Utf8DecodeError::truncated, byte_offset);
    }

    const auto lead = byte_at(input, byte_offset);
    if (lead <= ascii_max) {
        return DecodedScalar{.value = static_cast<char32_t>(lead),
                             .byte_count = 1U};
    }
    if (lead >= continuation_first && lead <= continuation_last) {
        return decode_failure(Utf8DecodeError::unexpected_continuation,
                              byte_offset);
    }
    if (lead == overlong_two_first || lead == overlong_two_last) {
        return decode_failure(Utf8DecodeError::overlong, byte_offset);
    }
    if (lead >= out_of_range_lead_first && lead <= out_of_range_lead_last) {
        return decode_failure(Utf8DecodeError::out_of_range, byte_offset);
    }
    if (lead >= invalid_lead_first) {
        return decode_failure(Utf8DecodeError::invalid_lead, byte_offset);
    }

    const auto continuation = [&](const std::size_t relative)
        -> std::expected<std::uint8_t, Utf8DecodeFailure> {
        // byte_offset is known to be in range. Checking the remaining length
        // before addition makes the arithmetic total even for a synthetic
        // string_view whose size approaches size_t::max().
        if (relative >= input.size() - byte_offset) {
            return decode_failure(Utf8DecodeError::truncated, input.size());
        }
        const auto position = byte_offset + relative;
        const auto value = byte_at(input, position);
        if (!is_continuation(value)) {
            return decode_failure(Utf8DecodeError::invalid_continuation,
                                  position);
        }
        return value;
    };

    const auto second = continuation(1U);
    if (!second) {
        return std::unexpected(second.error());
    }

    if (lead <= two_byte_lead_last) {
        const auto scalar = static_cast<char32_t>(
            (static_cast<std::uint32_t>(lead & 0x1FU) << 6U) |
            static_cast<std::uint32_t>(*second & 0x3FU));
        return DecodedScalar{.value = scalar, .byte_count = 2U};
    }

    const auto third = continuation(2U);
    if (!third) {
        return std::unexpected(third.error());
    }

    if (lead <= three_byte_lead_last) {
        if (lead == three_byte_lead_first && *second < three_byte_second_min) {
            return decode_failure(Utf8DecodeError::overlong, byte_offset);
        }
        if (lead == surrogate_lead && *second >= surrogate_second_min) {
            return decode_failure(Utf8DecodeError::surrogate, byte_offset);
        }
        const auto scalar = static_cast<char32_t>(
            (static_cast<std::uint32_t>(lead & 0x0FU) << 12U) |
            (static_cast<std::uint32_t>(*second & 0x3FU) << 6U) |
            static_cast<std::uint32_t>(*third & 0x3FU));
        return DecodedScalar{.value = scalar, .byte_count = 3U};
    }

    const auto fourth = continuation(3U);
    if (!fourth) {
        return std::unexpected(fourth.error());
    }
    if (lead == four_byte_lead_first && *second < four_byte_second_min) {
        return decode_failure(Utf8DecodeError::overlong, byte_offset);
    }
    if (lead == four_byte_lead_last && *second > four_byte_second_max) {
        return decode_failure(Utf8DecodeError::out_of_range, byte_offset);
    }

    const auto scalar = static_cast<char32_t>(
        (static_cast<std::uint32_t>(lead & 0x07U) << 18U) |
        (static_cast<std::uint32_t>(*second & 0x3FU) << 12U) |
        (static_cast<std::uint32_t>(*third & 0x3FU) << 6U) |
        static_cast<std::uint32_t>(*fourth & 0x3FU));
    return DecodedScalar{.value = scalar, .byte_count = 4U};
}

std::string_view LatinSurface::slice(const std::size_t begin,
                                     const std::size_t count) const noexcept {
    if (begin > quantities.size() || count > quantities.size() - begin ||
        nfc_byte_offsets.size() != quantities.size() + 1U) {
        return {};
    }
    const auto first = static_cast<std::size_t>(nfc_byte_offsets[begin]);
    const auto last = static_cast<std::size_t>(nfc_byte_offsets[begin + count]);
    if (first > last || last > normalized_nfc.size()) {
        return {};
    }
    return std::string_view{normalized_nfc}.substr(first, last - first);
}

std::string_view
LatinSurfaceView::slice(const std::size_t begin,
                        const std::size_t count) const noexcept {
    if (begin > quantities.size() || count > quantities.size() - begin ||
        nfc_byte_offsets.size() != quantities.size() + 1U) {
        return {};
    }
    const auto first = static_cast<std::size_t>(nfc_byte_offsets[begin]);
    const auto last = static_cast<std::size_t>(nfc_byte_offsets[begin + count]);
    if (first > last || last > normalized_nfc.size()) {
        return {};
    }
    return normalized_nfc.substr(first, last - first);
}

std::expected<LatinSurfaceRequirements, LatinSurfaceError>
LatinSurfaceNormalizer::requirements(const std::string_view input) const {
    if (is_all_ascii_letters(input)) {
        const auto maximum =
            static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max());
        if (input.size() > maximum) {
            return std::unexpected(
                surface_error(LatinSurfaceErrorCode::input_too_large, 0U,
                              "normalized input exceeds range limits"));
        }
        return LatinSurfaceRequirements{
            .logical_letters = input.size(),
            .normalized_nfc_bytes = input.size(),
            .ascii_only = true,
        };
    }
    const auto validation = validate_finite_input(input);
    if (!validation) {
        return std::unexpected(validation.error());
    }
    if (input.empty()) {
        return std::unexpected(surface_error(LatinSurfaceErrorCode::empty_input,
                                             0U, "Latin word is empty"));
    }

    LatinSurfaceRequirements result;
    std::optional<LatinGlyph> pending;
    std::size_t byte_offset{};
    while (byte_offset < input.size()) {
        const auto decoded = decode_utf8_scalar(input, byte_offset);
        const auto mapping = map_latin_codepoint(decoded->value);
        if (mapping->kind == LatinCodepointKind::base_letter) {
            if (pending) {
                const auto accounted = account_glyph(result, *pending);
                if (!accounted) {
                    return std::unexpected(accounted.error());
                }
            }
            pending = mapping->glyph;
        } else {
            if (!pending || !detail::is_vowel(pending->base) ||
                pending->quantity != LatinQuantity::unknown) {
                return std::unexpected(surface_error(
                    LatinSurfaceErrorCode::invalid_vowel_quantity, byte_offset,
                    "macron or breve is misplaced, duplicated, or "
                    "conflicting"));
            }
            pending->quantity = mapping->glyph.quantity;
        }
        byte_offset += decoded->byte_count;
    }
    if (pending) {
        const auto accounted = account_glyph(result, *pending);
        if (!accounted) {
            return std::unexpected(accounted.error());
        }
    }
    return result;
}

std::expected<LatinSurfaceView, LatinSurfaceError>
LatinSurfaceNormalizer::normalize_into(
    const std::string_view input, const LatinSurfaceBuffers buffers) const {
    const auto required = requirements(input);
    if (!required) {
        return std::unexpected(required.error());
    }
    if (buffers.normalized_nfc.size() < required->normalized_nfc_bytes ||
        buffers.orthography_ascii.size() < required->logical_letters ||
        buffers.lookup_ascii.size() < required->logical_letters ||
        buffers.quantities.size() < required->logical_letters ||
        buffers.nfc_byte_offsets.size() <= required->logical_letters) {
        return std::unexpected(surface_error(
            LatinSurfaceErrorCode::insufficient_output_space, 0U,
            "caller-provided Latin surface storage is too small"));
    }

    std::size_t normalized_size{};
    std::size_t logical_size{};
    buffers.nfc_byte_offsets[0] = 0U;
    const auto surface_view = [&] {
        return LatinSurfaceView{
            .original_utf8 = input,
            .normalized_nfc = std::string_view{buffers.normalized_nfc.data(),
                                               normalized_size},
            .orthography_ascii =
                std::string_view{buffers.orthography_ascii.data(),
                                 logical_size},
            .lookup_ascii =
                std::string_view{buffers.lookup_ascii.data(), logical_size},
            .quantities = buffers.quantities.first(logical_size),
            .nfc_byte_offsets =
                buffers.nfc_byte_offsets.first(logical_size + 1U),
        };
    };

    if (required->ascii_only) {
        for (const auto value : input) {
            const auto lowercase = detail::lowercase_ascii(value);
            buffers.normalized_nfc[logical_size] = lowercase;
            buffers.orthography_ascii[logical_size] = lowercase;
            buffers.lookup_ascii[logical_size] =
                detail::lookup_letter(lowercase);
            buffers.quantities[logical_size] = LatinQuantity::unknown;
            ++logical_size;
            ++normalized_size;
            buffers.nfc_byte_offsets[logical_size] =
                static_cast<std::uint32_t>(normalized_size);
        }
        return surface_view();
    }

    const auto emit = [&](const LatinGlyph glyph) {
        buffers.orthography_ascii[logical_size] = glyph.base;
        buffers.lookup_ascii[logical_size] = detail::lookup_letter(glyph.base);
        buffers.quantities[logical_size] = glyph.quantity;
        const auto encoded = encode_latin_glyph_nfc(glyph);
        for (const auto &scalar : *encoded) {
            for (std::uint8_t index = 0U; index < scalar.byte_count; ++index) {
                buffers.normalized_nfc[normalized_size] = scalar.bytes[index];
                ++normalized_size;
            }
        }
        ++logical_size;
        buffers.nfc_byte_offsets[logical_size] =
            static_cast<std::uint32_t>(normalized_size);
    };

    std::optional<LatinGlyph> pending;
    std::size_t byte_offset{};
    while (byte_offset < input.size()) {
        const auto decoded = decode_utf8_scalar(input, byte_offset);
        const auto mapping = map_latin_codepoint(decoded->value);
        if (mapping->kind == LatinCodepointKind::base_letter) {
            if (pending) {
                emit(*pending);
            }
            pending = mapping->glyph;
        } else {
            pending->quantity = mapping->glyph.quantity;
        }
        byte_offset += decoded->byte_count;
    }
    emit(*pending);

    return surface_view();
}

std::expected<LatinSurface, LatinSurfaceError>
LatinSurfaceNormalizer::normalize(const std::string_view input) const {
    if (is_all_ascii_letters(input)) {
        if (input.size() > static_cast<std::size_t>(
                               std::numeric_limits<std::int32_t>::max())) {
            return std::unexpected(
                surface_error(LatinSurfaceErrorCode::input_too_large, 0U,
                              "normalized input exceeds range limits"));
        }
        return normalize_ascii(input);
    }

    // Preserve the production lexer's error precedence: validate the complete
    // original stream and its allowlist before interpreting quantity marks.
    const auto validation = validate_finite_input(input);
    if (!validation) {
        return std::unexpected(validation.error());
    }

    if (input.empty()) {
        return std::unexpected(surface_error(LatinSurfaceErrorCode::empty_input,
                                             0U, "Latin word is empty"));
    }

    LatinSurface result;
    result.original_utf8.assign(input);
    result.normalized_nfc.reserve(input.size());
    result.orthography_ascii.reserve(input.size());
    result.lookup_ascii.reserve(input.size());
    result.quantities.reserve(input.size());
    result.nfc_byte_offsets.reserve(input.size() + 1U);
    result.nfc_byte_offsets.push_back(0U);

    std::optional<LatinGlyph> pending;
    std::size_t byte_offset{};
    while (byte_offset < input.size()) {
        const auto decoded = decode_utf8_scalar(input, byte_offset);
        if (!decoded) {
            return std::unexpected(surface_error(
                LatinSurfaceErrorCode::invalid_utf8,
                decoded.error().byte_offset, "input is not valid UTF-8",
                decoded.error().code));
        }
        const auto mapping = map_latin_codepoint(decoded->value);
        if (!mapping) {
            return std::unexpected(surface_error(
                LatinSurfaceErrorCode::unsupported_character, byte_offset,
                "input contains a character outside the finite Latin "
                "alphabet"));
        }

        if (mapping->kind == LatinCodepointKind::base_letter) {
            if (pending) {
                auto appended = append_glyph(result, *pending);
                if (!appended) {
                    return std::unexpected(appended.error());
                }
            }
            pending = mapping->glyph;
        } else {
            if (!pending || !detail::is_vowel(pending->base) ||
                pending->quantity != LatinQuantity::unknown) {
                return std::unexpected(surface_error(
                    LatinSurfaceErrorCode::invalid_vowel_quantity, byte_offset,
                    "macron or breve is misplaced, duplicated, or "
                    "conflicting"));
            }
            pending->quantity = mapping->glyph.quantity;
        }
        byte_offset += decoded->byte_count;
    }

    if (pending) {
        auto appended = append_glyph(result, *pending);
        if (!appended) {
            return std::unexpected(appended.error());
        }
    }
    return result;
}

} // namespace words::poc::latin_unicode
