#include "latin_utf8.hpp"

#include "words/lexer.hpp"

#include <utf8proc.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <fstream>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace words::poc::latin_unicode {
namespace {

struct ReferenceRow final {
    char32_t codepoint;
    char32_t decomposed_base;
    char32_t mark;
    char32_t normalized_nfc;
    std::string_view utf8;
    std::string_view decomposed_utf8;
};

constexpr std::array<ReferenceRow, 22> reference_rows{{
    {0x0100, U'A', 0x0304, 0x0101, "\xC4\x80", "A\xCC\x84"},
    {0x0101, U'a', 0x0304, 0x0101, "\xC4\x81", "a\xCC\x84"},
    {0x0102, U'A', 0x0306, 0x0103, "\xC4\x82", "A\xCC\x86"},
    {0x0103, U'a', 0x0306, 0x0103, "\xC4\x83", "a\xCC\x86"},
    {0x0112, U'E', 0x0304, 0x0113, "\xC4\x92", "E\xCC\x84"},
    {0x0113, U'e', 0x0304, 0x0113, "\xC4\x93", "e\xCC\x84"},
    {0x0114, U'E', 0x0306, 0x0115, "\xC4\x94", "E\xCC\x86"},
    {0x0115, U'e', 0x0306, 0x0115, "\xC4\x95", "e\xCC\x86"},
    {0x012A, U'I', 0x0304, 0x012B, "\xC4\xAA", "I\xCC\x84"},
    {0x012B, U'i', 0x0304, 0x012B, "\xC4\xAB", "i\xCC\x84"},
    {0x012C, U'I', 0x0306, 0x012D, "\xC4\xAC", "I\xCC\x86"},
    {0x012D, U'i', 0x0306, 0x012D, "\xC4\xAD", "i\xCC\x86"},
    {0x014C, U'O', 0x0304, 0x014D, "\xC5\x8C", "O\xCC\x84"},
    {0x014D, U'o', 0x0304, 0x014D, "\xC5\x8D", "o\xCC\x84"},
    {0x014E, U'O', 0x0306, 0x014F, "\xC5\x8E", "O\xCC\x86"},
    {0x014F, U'o', 0x0306, 0x014F, "\xC5\x8F", "o\xCC\x86"},
    {0x016A, U'U', 0x0304, 0x016B, "\xC5\xAA", "U\xCC\x84"},
    {0x016B, U'u', 0x0304, 0x016B, "\xC5\xAB", "u\xCC\x84"},
    {0x016C, U'U', 0x0306, 0x016D, "\xC5\xAC", "U\xCC\x86"},
    {0x016D, U'u', 0x0306, 0x016D, "\xC5\xAD", "u\xCC\x86"},
    {0x0232, U'Y', 0x0304, 0x0233, "\xC8\xB2", "Y\xCC\x84"},
    {0x0233, U'y', 0x0304, 0x0233, "\xC8\xB3", "y\xCC\x84"},
}};

constexpr auto accepted_input_alphabet = [] {
    std::array<char32_t, 76> result{};
    std::size_t index{};
    for (char32_t value = U'A'; value <= U'Z'; ++value) {
        result[index++] = value;
    }
    for (char32_t value = U'a'; value <= U'z'; ++value) {
        result[index++] = value;
    }
    result[index++] = detail::combining_macron;
    result[index++] = detail::combining_breve;
    for (const auto &entry : detail::special_latin_mappings) {
        result[index++] = entry.input;
    }
    if (index != result.size()) {
        throw "finite Latin alphabet size is inconsistent";
    }
    return result;
}();

[[nodiscard]] std::string encoded(const char32_t codepoint) {
    const auto result = encode_utf8_scalar(codepoint);
    EXPECT_TRUE(result.has_value());
    if (!result) {
        return {};
    }
    return std::string{result->bytes.data(), result->byte_count};
}

[[nodiscard]] std::expected<std::string, utf8proc_ssize_t>
oracle_map(const std::string_view input, const utf8proc_option_t options) {
    utf8proc_uint8_t *output{};
    const auto size = utf8proc_map(
        reinterpret_cast<const utf8proc_uint8_t *>(input.data()),
        static_cast<utf8proc_ssize_t>(input.size()), &output, options);
    if (size < 0) {
        return std::unexpected(size);
    }
    const std::string result{reinterpret_cast<const char *>(output),
                             static_cast<std::size_t>(size)};
    utf8proc_free(output);
    return result;
}

[[nodiscard]] LatinQuantity from_oracle_quantity(const VowelQuantity quantity) {
    if (quantity == VowelQuantity::unknown) {
        return LatinQuantity::unknown;
    }
    if (quantity == VowelQuantity::short_vowel) {
        return LatinQuantity::short_vowel;
    }
    if (quantity == VowelQuantity::long_vowel) {
        return LatinQuantity::long_vowel;
    }
    return LatinQuantity::unknown;
}

[[nodiscard]] std::optional<LatinSurfaceErrorCode>
from_oracle_error(const DiagnosticCode code) {
    if (code == DiagnosticCode::empty_input) {
        return LatinSurfaceErrorCode::empty_input;
    }
    if (code == DiagnosticCode::input_too_large) {
        return LatinSurfaceErrorCode::input_too_large;
    }
    if (code == DiagnosticCode::invalid_utf8) {
        return LatinSurfaceErrorCode::invalid_utf8;
    }
    if (code == DiagnosticCode::invalid_vowel_quantity) {
        return LatinSurfaceErrorCode::invalid_vowel_quantity;
    }
    if (code == DiagnosticCode::unsupported_character) {
        return LatinSurfaceErrorCode::unsupported_character;
    }
    return std::nullopt;
}

[[nodiscard]] bool equivalent_surface(const LatinSurface &candidate,
                                      const SurfaceForm &oracle) {
    if (candidate.original_utf8 != oracle.original_utf8 ||
        candidate.normalized_nfc != oracle.normalized_nfc ||
        candidate.orthography_ascii != oracle.orthography_ascii ||
        candidate.lookup_ascii != oracle.lookup_ascii ||
        candidate.nfc_byte_offsets != oracle.nfc_byte_offsets ||
        candidate.quantities.size() != oracle.quantities.size()) {
        return false;
    }
    for (std::size_t index = 0; index < candidate.quantities.size(); ++index) {
        if (candidate.quantities[index] !=
            from_oracle_quantity(oracle.quantities[index])) {
            return false;
        }
        if (candidate.slice(index, 1U) !=
            oracle.slice(SurfaceRange{
                .begin = static_cast<std::uint32_t>(index), .count = 1U})) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool equivalent_surface(const LatinSurfaceView &candidate,
                                      const SurfaceForm &oracle) {
    if (candidate.original_utf8 != oracle.original_utf8 ||
        candidate.normalized_nfc != oracle.normalized_nfc ||
        candidate.orthography_ascii != oracle.orthography_ascii ||
        candidate.lookup_ascii != oracle.lookup_ascii ||
        !std::ranges::equal(candidate.nfc_byte_offsets,
                            oracle.nfc_byte_offsets) ||
        candidate.quantities.size() != oracle.quantities.size()) {
        return false;
    }
    for (std::size_t index = 0; index < candidate.quantities.size(); ++index) {
        if (candidate.quantities[index] !=
                from_oracle_quantity(oracle.quantities[index]) ||
            candidate.slice(index, 1U) !=
                oracle.slice(SurfaceRange{
                    .begin = static_cast<std::uint32_t>(index), .count = 1U})) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool semantically_equal(const LatinSurface &left,
                                      const LatinSurface &right) {
    return left.normalized_nfc == right.normalized_nfc &&
           left.orthography_ascii == right.orthography_ascii &&
           left.lookup_ascii == right.lookup_ascii &&
           left.quantities == right.quantities &&
           left.nfc_byte_offsets == right.nfc_byte_offsets;
}

void expect_differential(const std::string_view input) {
    const LatinSurfaceNormalizer candidate_normalizer;
    const LatinLexer oracle_lexer;
    const auto candidate = candidate_normalizer.normalize(input);
    const auto required = candidate_normalizer.requirements(input);
    const auto oracle = oracle_lexer.lex(input);
    ASSERT_EQ(candidate.has_value(), oracle.has_value())
        << "input bytes: " << testing::PrintToString(input);
    ASSERT_EQ(required.has_value(), candidate.has_value())
        << "input bytes: " << testing::PrintToString(input);
    if (candidate && oracle) {
        EXPECT_TRUE(equivalent_surface(*candidate, *oracle))
            << "input bytes: " << testing::PrintToString(input);

        constexpr std::size_t scratch_capacity = 256U;
        ASSERT_LE(input.size(), scratch_capacity);
        std::array<char, scratch_capacity> normalized{};
        std::array<char, scratch_capacity> orthography{};
        std::array<char, scratch_capacity> lookup{};
        std::array<LatinQuantity, scratch_capacity> quantities{};
        std::array<std::uint32_t, scratch_capacity + 1U> offsets{};
        const auto view = candidate_normalizer.normalize_into(
            input, LatinSurfaceBuffers{
                       .normalized_nfc = normalized,
                       .orthography_ascii = orthography,
                       .lookup_ascii = lookup,
                       .quantities = quantities,
                       .nfc_byte_offsets = offsets,
                   });
        ASSERT_TRUE(view.has_value());
        EXPECT_TRUE(equivalent_surface(*view, *oracle));
        return;
    }
    ASSERT_FALSE(candidate);
    ASSERT_FALSE(required);
    ASSERT_FALSE(oracle);
    const auto expected = from_oracle_error(oracle.error().code);
    ASSERT_TRUE(expected.has_value());
    EXPECT_EQ(candidate.error().code, *expected)
        << "input bytes: " << testing::PrintToString(input);
    EXPECT_EQ(required.error().code, candidate.error().code)
        << "input bytes: " << testing::PrintToString(input);
}

[[nodiscard]] bool
strict_and_oracle_decode_equally(const std::string_view input) {
    std::size_t candidate_offset{};
    std::size_t oracle_offset{};
    while (candidate_offset < input.size() && oracle_offset < input.size()) {
        const auto candidate = decode_utf8_scalar(input, candidate_offset);
        utf8proc_int32_t oracle_codepoint{};
        const auto oracle_size = utf8proc_iterate(
            reinterpret_cast<const utf8proc_uint8_t *>(input.data()) +
                oracle_offset,
            static_cast<utf8proc_ssize_t>(input.size() - oracle_offset),
            &oracle_codepoint);
        if (!candidate || oracle_size <= 0) {
            return !candidate && oracle_size <= 0;
        }
        if (candidate->value != static_cast<char32_t>(oracle_codepoint) ||
            candidate->byte_count != static_cast<std::uint8_t>(oracle_size)) {
            return false;
        }
        candidate_offset += candidate->byte_count;
        oracle_offset += static_cast<std::size_t>(oracle_size);
    }
    return candidate_offset == input.size() && oracle_offset == input.size();
}

TEST(LatinUnicodeReferenceTest, ConstexprTableMatchesVendoredOracle) {
    for (const auto &row : reference_rows) {
        EXPECT_EQ(encoded(row.codepoint), row.utf8);

        const auto decomposition_options = static_cast<utf8proc_option_t>(
            UTF8PROC_STABLE | UTF8PROC_DECOMPOSE);
        const auto decomposition = oracle_map(row.utf8, decomposition_options);
        ASSERT_TRUE(decomposition.has_value());
        EXPECT_EQ(*decomposition, row.decomposed_utf8);
        EXPECT_EQ(*decomposition,
                  encoded(row.decomposed_base) + encoded(row.mark));

        const auto folded_options = static_cast<utf8proc_option_t>(
            UTF8PROC_STABLE | UTF8PROC_DECOMPOSE | UTF8PROC_CASEFOLD);
        const auto folded = oracle_map(row.utf8, folded_options);
        ASSERT_TRUE(folded.has_value());
        const auto compose_options =
            static_cast<utf8proc_option_t>(UTF8PROC_STABLE | UTF8PROC_COMPOSE);
        const auto normalized = oracle_map(*folded, compose_options);
        ASSERT_TRUE(normalized.has_value());
        EXPECT_EQ(*normalized, encoded(row.normalized_nfc));
    }
    EXPECT_EQ(encoded(detail::combining_macron), "\xCC\x84");
    EXPECT_EQ(encoded(detail::combining_breve), "\xCC\x86");
}

TEST(LatinUnicodeDifferentialTest, CoversEveryAsciiLetter) {
    for (char value = 'A'; value <= 'Z'; ++value) {
        expect_differential(std::string_view{&value, 1U});
    }
    for (char value = 'a'; value <= 'z'; ++value) {
        expect_differential(std::string_view{&value, 1U});
    }
}

TEST(LatinUnicodeDifferentialTest, CoversEveryPrecomposedAndDecomposedForm) {
    const LatinSurfaceNormalizer normalizer;
    for (const auto &row : reference_rows) {
        expect_differential(row.utf8);
        expect_differential(row.decomposed_utf8);
        const auto precomposed = normalizer.normalize(row.utf8);
        const auto decomposed = normalizer.normalize(row.decomposed_utf8);
        ASSERT_TRUE(precomposed.has_value());
        ASSERT_TRUE(decomposed.has_value());
        EXPECT_TRUE(semantically_equal(*precomposed, *decomposed));
    }
}

TEST(LatinUnicodeDifferentialTest, CoversRequestedWordsAndOrthographicFolding) {
    constexpr std::array inputs{
        std::string_view{"puella"},
        std::string_view{"puell\xC4\x81"},
        std::string_view{"puella\xCC\x84"},
        std::string_view{"puell\xC4\x83"},
        std::string_view{"puella\xCC\x86"},
        std::string_view{"malum"},
        std::string_view{"m\xC4\x81lum"},
        std::string_view{"ma\xCC\x84lum"},
        std::string_view{"m\xC4\x83lum"},
        std::string_view{"ma\xCC\x86lum"},
        std::string_view{"juvenis"},
        std::string_view{"iuuenis"},
        std::string_view{"J\xC5\xAA\x56\xC4\x94NIS"},
        std::string_view{"y"},
        std::string_view{"\xC8\xB3"},
        std::string_view{"y\xCC\x84"},
        std::string_view{"Y\xCC\x84"},
        std::string_view{"y\xCC\x86"},
        std::string_view{"Y\xCC\x86"},
    };
    for (const auto input : inputs) {
        expect_differential(input);
    }

    const auto traditional = LatinSurfaceNormalizer{}.normalize("juvenis");
    const auto canonical = LatinSurfaceNormalizer{}.normalize("iuuenis");
    ASSERT_TRUE(traditional.has_value());
    ASSERT_TRUE(canonical.has_value());
    EXPECT_EQ(traditional->orthography_ascii, "juvenis");
    EXPECT_EQ(canonical->orthography_ascii, "iuuenis");
    EXPECT_EQ(traditional->lookup_ascii, canonical->lookup_ascii);
}

TEST(LatinUnicodeDifferentialTest, GeneratesMarkedVowelsInWordContexts) {
    constexpr std::string_view vowels = "aeiouyAEIOUY";
    constexpr std::array marks{detail::combining_macron,
                               detail::combining_breve};
    for (const auto vowel : vowels) {
        for (const auto mark : marks) {
            const auto marked = std::string{vowel} + encoded(mark);
            expect_differential(marked);
            expect_differential("b" + marked + "t");
            expect_differential(marked + "ba");
            expect_differential("ba" + marked);
        }
    }
}

TEST(LatinUnicodeDifferentialTest, RejectsInvalidQuantityPlacements) {
    constexpr std::string_view macron = "\xCC\x84";
    constexpr std::string_view breve = "\xCC\x86";
    constexpr std::array<std::string_view, 12> fixed{
        macron,
        breve,
        "\xCC\x84"
        "a",
        "\xCC\x86"
        "a",
        "a\xCC\x84\xCC\x84",
        "a\xCC\x86\xCC\x86",
        "a\xCC\x84\xCC\x86",
        "a\xCC\x86\xCC\x84",
        "a\xCC\x84\xCC\x86\xCC\x84",
        "\xC4\x81\xCC\x84",
        "\xC4\x83\xCC\x86",
        "a\xCC\x84\xCC\x84\xCC\x84",
    };
    for (const auto input : fixed) {
        expect_differential(input);
    }

    constexpr std::string_view consonants = "bcdfghjklmnpqrstvwxz";
    for (const auto consonant : consonants) {
        const auto base = std::string{consonant};
        expect_differential(base + std::string{macron});
        expect_differential(base + std::string{breve});
    }
}

TEST(LatinUnicodeDifferentialTest, RejectsCharactersOutsideFiniteDomain) {
    const std::string nul{"\0", 1U};
    constexpr std::array rejected{
        std::string_view{"á"},        std::string_view{"à"},
        std::string_view{"â"},        std::string_view{"ä"},
        std::string_view{"æ"},        std::string_view{"œ"},
        std::string_view{"ß"},        std::string_view{"K"},
        std::string_view{"ſ"},        std::string_view{"Ａ"},
        std::string_view{"α"},        std::string_view{"Ж"},
        std::string_view{"😀"},       std::string_view{"ﬁ"},
        std::string_view{"\xCC\x81"}, std::string_view{"\xEF\xBF\xBD"},
        std::string_view{"\x01", 1U}, std::string_view{"\x7F", 1U},
    };
    expect_differential(nul);
    expect_differential("");
    for (const auto input : rejected) {
        expect_differential(input);
    }
}

TEST(LatinUtf8DecoderTest, ClassifiesRequiredMalformedByteSequences) {
    struct Case final {
        std::string_view bytes;
        Utf8DecodeError expected;
    };
    constexpr std::array cases{
        Case{{"\x80", 1U}, Utf8DecodeError::unexpected_continuation},
        Case{{"\xC0\x80", 2U}, Utf8DecodeError::overlong},
        Case{{"\xC1\xBF", 2U}, Utf8DecodeError::overlong},
        Case{{"\xC2", 1U}, Utf8DecodeError::truncated},
        Case{{"\xE0", 1U}, Utf8DecodeError::truncated},
        Case{{"\xE1\x80", 2U}, Utf8DecodeError::truncated},
        Case{{"\xE0\x80\x80", 3U}, Utf8DecodeError::overlong},
        Case{{"\xED\xA0\x80", 3U}, Utf8DecodeError::surrogate},
        Case{{"\xF0", 1U}, Utf8DecodeError::truncated},
        Case{{"\xF0\x90", 2U}, Utf8DecodeError::truncated},
        Case{{"\xF0\x90\x80", 3U}, Utf8DecodeError::truncated},
        Case{{"\xF0\x80\x80\x80", 4U}, Utf8DecodeError::overlong},
        Case{{"\xF4\x90\x80\x80", 4U}, Utf8DecodeError::out_of_range},
        Case{{"\xF5\x80\x80\x80", 4U}, Utf8DecodeError::out_of_range},
        Case{{"\xFF", 1U}, Utf8DecodeError::invalid_lead},
        Case{{"\xC2\x41", 2U}, Utf8DecodeError::invalid_continuation},
    };
    for (const auto &test : cases) {
        const auto decoded = decode_utf8_scalar(test.bytes);
        ASSERT_FALSE(decoded.has_value());
        EXPECT_EQ(decoded.error().code, test.expected);
        expect_differential(test.bytes);
    }

    const std::string valid_then_invalid{"a\x80", 2U};
    const std::string invalid_in_word{"pu\xFF"
                                      "ella",
                                      7U};
    expect_differential(valid_then_invalid);
    expect_differential(invalid_in_word);
}

TEST(LatinUtf8DecoderTest, RejectsEveryTruncationOfAcceptedMultibyteScalars) {
    for (const auto &entry : detail::special_latin_mappings) {
        const auto bytes = encoded(entry.input);
        for (std::size_t size = 1U; size < bytes.size(); ++size) {
            expect_differential(std::string_view{bytes}.substr(0U, size));
        }
    }
    for (const auto mark :
         {detail::combining_macron, detail::combining_breve}) {
        const auto bytes = encoded(mark);
        for (std::size_t size = 1U; size < bytes.size(); ++size) {
            expect_differential(std::string{"a"} + bytes.substr(0U, size));
        }
    }
}

TEST(LatinUnicodeDifferentialTest, MutatesValidLatinInputsDeterministically) {
    const std::array seeds{
        std::string{"puella"},
        std::string{"puella\xCC\x84"},
        std::string{"J\xC5\xAAV\xC4\x94NIS"},
        std::string{"y\xCC\x86"},
    };
    constexpr std::array<std::uint8_t, 9> inserted{
        0x00U, 0x7FU, 0x80U, 0xBFU, 0xC0U, 0xC1U, 0xF4U, 0xF5U, 0xFFU,
    };
    for (const auto &seed : seeds) {
        for (std::size_t size = 0U; size < seed.size(); ++size) {
            expect_differential(std::string_view{seed}.substr(0U, size));
        }
        for (std::size_t offset = 0U; offset < seed.size(); ++offset) {
            for (std::uint8_t bit = 0U; bit < 8U; ++bit) {
                auto mutated = seed;
                const auto original = static_cast<std::uint8_t>(
                    static_cast<unsigned char>(mutated[offset]));
                mutated[offset] =
                    static_cast<char>(original ^ (std::uint8_t{1U} << bit));
                expect_differential(mutated);
            }
        }
        for (std::size_t offset = 0U; offset <= seed.size(); ++offset) {
            for (const auto value : inserted) {
                auto mutated = seed;
                mutated.insert(offset, 1U, static_cast<char>(value));
                expect_differential(mutated);
            }
            for (const auto &mark :
                 {std::string{"\xCC\x84"}, std::string{"\xCC\x86"}}) {
                auto mutated = seed;
                mutated.insert(offset, mark);
                expect_differential(mutated);
            }
        }
    }
}

TEST(LatinUnicodePropertyTest, NormalizationIsIdempotentSemantically) {
    std::vector<std::string> inputs{"puella", "juvenis", "JUVENIS",
                                    "y\xCC\x86"};
    for (const auto &row : reference_rows) {
        inputs.emplace_back(row.utf8);
        inputs.emplace_back(row.decomposed_utf8);
    }
    const LatinSurfaceNormalizer normalizer;
    for (const auto &input : inputs) {
        const auto first = normalizer.normalize(input);
        ASSERT_TRUE(first.has_value());
        const auto second = normalizer.normalize(first->normalized_nfc);
        ASSERT_TRUE(second.has_value());
        EXPECT_TRUE(semantically_equal(*first, *second));
        EXPECT_TRUE(strict_and_oracle_decode_equally(first->normalized_nfc));
    }
}

TEST(LatinUnicodePropertyTest, AcceptedScalarsRoundTripUtf8Codec) {
    std::vector<char32_t> accepted{detail::combining_macron,
                                   detail::combining_breve};
    for (char32_t value = U'A'; value <= U'Z'; ++value) {
        accepted.push_back(value);
    }
    for (char32_t value = U'a'; value <= U'z'; ++value) {
        accepted.push_back(value);
    }
    for (const auto &entry : detail::special_latin_mappings) {
        accepted.push_back(entry.input);
    }
    for (const auto value : accepted) {
        const auto bytes = encoded(value);
        const auto decoded = decode_utf8_scalar(bytes);
        ASSERT_TRUE(decoded.has_value());
        EXPECT_EQ(decoded->value, value);
        EXPECT_EQ(decoded->byte_count, bytes.size());
    }
    EXPECT_EQ(encode_utf8_scalar(0xD800).error(), Utf8EncodeError::surrogate);
    EXPECT_EQ(encode_utf8_scalar(0x110000).error(),
              Utf8EncodeError::out_of_range);
}

TEST(LatinUnicodePropertyTest, CallerStorageUsesExactRequirements) {
    constexpr std::string_view input = "Y\xCC\x86";
    const LatinSurfaceNormalizer normalizer;
    const auto required = normalizer.requirements(input);
    ASSERT_TRUE(required.has_value());
    EXPECT_EQ(required->logical_letters, 1U);
    EXPECT_EQ(required->normalized_nfc_bytes, 3U);

    std::array<char, 3> normalized{};
    std::array<char, 1> orthography{};
    std::array<char, 1> lookup{};
    std::array<LatinQuantity, 1> quantities{};
    std::array<std::uint32_t, 2> offsets{};
    const auto result =
        normalizer.normalize_into(input, LatinSurfaceBuffers{
                                             .normalized_nfc = normalized,
                                             .orthography_ascii = orthography,
                                             .lookup_ascii = lookup,
                                             .quantities = quantities,
                                             .nfc_byte_offsets = offsets,
                                         });
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->normalized_nfc, "y\xCC\x86");
    EXPECT_EQ(result->orthography_ascii, "y");
    EXPECT_EQ(result->lookup_ascii, "y");
    EXPECT_EQ(result->quantities.front(), LatinQuantity::short_vowel);
    EXPECT_EQ(result->slice(0U, 1U), result->normalized_nfc);

    const auto expect_insufficient_without_writes =
        [&](const LatinSurfaceBuffers buffers) {
            normalized.fill('#');
            orthography.fill('#');
            lookup.fill('#');
            quantities.fill(LatinQuantity::long_vowel);
            offsets.fill(99U);

            const auto too_small = normalizer.normalize_into(input, buffers);
            ASSERT_FALSE(too_small.has_value());
            EXPECT_EQ(too_small.error().code,
                      LatinSurfaceErrorCode::insufficient_output_space);
            EXPECT_TRUE(std::ranges::all_of(
                normalized, [](const char value) { return value == '#'; }));
            EXPECT_EQ(orthography.front(), '#');
            EXPECT_EQ(lookup.front(), '#');
            EXPECT_EQ(quantities.front(), LatinQuantity::long_vowel);
            EXPECT_TRUE(
                std::ranges::all_of(offsets, [](const std::uint32_t value) {
                    return value == 99U;
                }));
        };

    expect_insufficient_without_writes(LatinSurfaceBuffers{
        .normalized_nfc = std::span{normalized}.first(2U),
        .orthography_ascii = orthography,
        .lookup_ascii = lookup,
        .quantities = quantities,
        .nfc_byte_offsets = offsets,
    });
    expect_insufficient_without_writes(LatinSurfaceBuffers{
        .normalized_nfc = normalized,
        .orthography_ascii = std::span{orthography}.first(0U),
        .lookup_ascii = lookup,
        .quantities = quantities,
        .nfc_byte_offsets = offsets,
    });
    expect_insufficient_without_writes(LatinSurfaceBuffers{
        .normalized_nfc = normalized,
        .orthography_ascii = orthography,
        .lookup_ascii = std::span{lookup}.first(0U),
        .quantities = quantities,
        .nfc_byte_offsets = offsets,
    });
    expect_insufficient_without_writes(LatinSurfaceBuffers{
        .normalized_nfc = normalized,
        .orthography_ascii = orthography,
        .lookup_ascii = lookup,
        .quantities = std::span{quantities}.first(0U),
        .nfc_byte_offsets = offsets,
    });
    expect_insufficient_without_writes(LatinSurfaceBuffers{
        .normalized_nfc = normalized,
        .orthography_ascii = orthography,
        .lookup_ascii = lookup,
        .quantities = quantities,
        .nfc_byte_offsets = std::span{offsets}.first(1U),
    });

    const auto expect_invalid_without_writes =
        [&](const std::string_view invalid,
            const LatinSurfaceErrorCode expected_error) {
            normalized.fill('#');
            orthography.fill('#');
            lookup.fill('#');
            quantities.fill(LatinQuantity::long_vowel);
            offsets.fill(99U);
            const auto rejected = normalizer.normalize_into(
                invalid, LatinSurfaceBuffers{
                             .normalized_nfc = normalized,
                             .orthography_ascii = orthography,
                             .lookup_ascii = lookup,
                             .quantities = quantities,
                             .nfc_byte_offsets = offsets,
                         });
            ASSERT_FALSE(rejected.has_value());
            EXPECT_EQ(rejected.error().code, expected_error);
            EXPECT_TRUE(std::ranges::all_of(
                normalized, [](const char value) { return value == '#'; }));
            EXPECT_EQ(orthography.front(), '#');
            EXPECT_EQ(lookup.front(), '#');
            EXPECT_EQ(quantities.front(), LatinQuantity::long_vowel);
            EXPECT_TRUE(
                std::ranges::all_of(offsets, [](const std::uint32_t value) {
                    return value == 99U;
                }));
        };
    expect_invalid_without_writes(std::string_view{"\x80", 1U},
                                  LatinSurfaceErrorCode::invalid_utf8);
    expect_invalid_without_writes(
        "a\xCC\x84\xCC\x86", LatinSurfaceErrorCode::invalid_vowel_quantity);
}

TEST(LatinUnicodePropertyTest, SliceIsTotalForCorruptedPublicOffsets) {
    auto surface = LatinSurfaceNormalizer{}.normalize("ab");
    ASSERT_TRUE(surface.has_value());
    surface->nfc_byte_offsets = {0U, 3U, 2U};
    EXPECT_TRUE(surface->slice(0U, 1U).empty());
    EXPECT_TRUE(surface->slice(1U, 1U).empty());
    surface->nfc_byte_offsets = {0U, 1U, 99U};
    EXPECT_TRUE(surface->slice(1U, 1U).empty());
    surface->nfc_byte_offsets.clear();
    EXPECT_TRUE(surface->slice(0U, 1U).empty());
}

TEST(LatinUnicodeFiniteLanguageTest,
     EnumeratesEverySequenceOfUpToThreeAcceptedScalars) {
    std::uint64_t cases = 1U;
    for (std::size_t length = 1U; length <= 3U; ++length) {
        cases *= accepted_input_alphabet.size();
        for (std::uint64_t ordinal = 0U; ordinal < cases; ++ordinal) {
            auto remaining = ordinal;
            std::string input;
            input.reserve(length * 3U);
            for (std::size_t position = 0U; position < length; ++position) {
                const auto alphabet_index = static_cast<std::size_t>(
                    remaining % accepted_input_alphabet.size());
                input += encoded(accepted_input_alphabet[alphabet_index]);
                remaining /= accepted_input_alphabet.size();
            }
            expect_differential(input);
        }
    }
}

TEST(LatinUnicodeCorpusTest, MatchesOracleForFrozenLatinLibrarySample) {
    std::ifstream corpus{WORDS_LATIN_POC_CORPUS};
    ASSERT_TRUE(corpus.good());
    std::string line;
    std::size_t tested{};
    while (std::getline(corpus, line)) {
        if (line.empty() || line.front() == '#') {
            continue;
        }
        const auto separator = line.find('\t');
        ASSERT_NE(separator, std::string::npos);
        const auto word = std::string_view{line}.substr(0U, separator);
        expect_differential(word);

        const auto options = static_cast<utf8proc_option_t>(UTF8PROC_STABLE |
                                                            UTF8PROC_DECOMPOSE);
        const auto decomposed = oracle_map(word, options);
        ASSERT_TRUE(decomposed.has_value());
        expect_differential(*decomposed);
        ++tested;
    }
    EXPECT_EQ(tested, 212U);
}

TEST(LatinUnicodeExhaustiveTest, MatchesOracleForEveryUnicodeScalar) {
    const LatinSurfaceNormalizer normalizer;
    const LatinLexer lexer;
    for (std::uint32_t value = 0U; value <= 0x10FFFFU; ++value) {
        if (value >= 0xD800U && value <= 0xDFFFU) {
            continue;
        }
        const auto input = encoded(static_cast<char32_t>(value));
        const auto candidate = normalizer.normalize(input);
        const auto oracle = lexer.lex(input);
        if (candidate.has_value() != oracle.has_value()) {
            FAIL() << "acceptance differs at U+" << std::hex << value;
        }
        if (candidate && oracle) {
            if (!equivalent_surface(*candidate, *oracle)) {
                FAIL() << "surface differs at U+" << std::hex << value;
            }
        } else {
            const auto expected = from_oracle_error(oracle.error().code);
            if (!expected || candidate.error().code != *expected) {
                FAIL() << "diagnostic differs at U+" << std::hex << value;
            }
        }
    }
}

TEST(LatinUtf8ExhaustiveTest, MatchesOracleForAllOneToThreeByteStrings) {
    std::array<char, 3> bytes{};
    for (std::uint32_t length = 1U; length <= 3U; ++length) {
        const auto cases = std::uint32_t{1U} << (length * 8U);
        for (std::uint32_t value = 0U; value < cases; ++value) {
            auto remaining = value;
            for (std::uint32_t index = 0U; index < length; ++index) {
                bytes[index] = static_cast<char>(remaining & 0xFFU);
                remaining >>= 8U;
            }
            const std::string_view input{bytes.data(), length};
            if (!strict_and_oracle_decode_equally(input)) {
                FAIL() << "decoder differs for length=" << length
                       << " value=" << std::hex << value;
            }
        }
    }
}

TEST(LatinUtf8ExhaustiveTest,
     MatchesOracleForFourByteStarterAndEveryFinalByte) {
    std::array<char, 4> bytes{};
    for (std::uint32_t lead = 0xF0U; lead <= 0xF7U; ++lead) {
        bytes[0] = static_cast<char>(lead);
        for (std::uint32_t prefix = 0U; prefix < (1U << 12U); ++prefix) {
            bytes[1] = static_cast<char>(0x80U | ((prefix >> 6U) & 0x3FU));
            bytes[2] = static_cast<char>(0x80U | (prefix & 0x3FU));
            for (std::uint32_t final = 0U; final <= 0xFFU; ++final) {
                bytes[3] = static_cast<char>(final);
                if (!strict_and_oracle_decode_equally(
                        std::string_view{bytes.data(), bytes.size()})) {
                    FAIL() << "decoder differs for lead=" << std::hex << lead
                           << " prefix=" << prefix << " final=" << final;
                }
            }
        }
    }
}

} // namespace
} // namespace words::poc::latin_unicode
