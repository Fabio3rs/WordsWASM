#include "unicode_backend.hpp"

#include "words/lexer.hpp"

#include <gtest/gtest.h>
#include <utf8proc.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <type_traits>

namespace words::poc::unicode_backend {
namespace {

template <typename Enum>
[[nodiscard]] constexpr auto value(const Enum item) noexcept {
    return std::to_underlying(item);
}

static_assert(value(boundary_flag_t::none) == value(BoundaryFlag::none));
static_assert(value(boundary_flag_t::quote) == value(BoundaryFlag::quote));
static_assert(value(boundary_flag_t::dash) == value(BoundaryFlag::dash));
static_assert(value(boundary_flag_t::bracket) == value(BoundaryFlag::bracket));
static_assert(value(boundary_flag_t::other_punctuation) ==
              value(BoundaryFlag::other_punctuation));

void expect_iterate_equal(const std::string_view input) {
    codepoint_t compact_codepoint = 123;
    codepoint_t full_codepoint = 456;
    const auto *const bytes = reinterpret_cast<const byte_t *>(input.data());
    const auto size = static_cast<ssize_t>(input.size());
    const auto compact_size = compact::iterate(bytes, size, &compact_codepoint);
    const auto full_size = full::iterate(bytes, size, &full_codepoint);
    ASSERT_EQ(compact_size, full_size);
    ASSERT_EQ(compact_codepoint, full_codepoint);
}

TEST(UnicodeBackendMetadataTest, PinsTheVendoredOracleVersions) {
    EXPECT_EQ(utf8proc_version, ::utf8proc_version());
    EXPECT_EQ(unicode_version, ::utf8proc_unicode_version());
    EXPECT_EQ(punctuation_codepoint_count, 856U);
}

TEST(UnicodeBackendCategoryTest, MatchesOracleForEveryUnicodeScalar) {
    std::uint32_t matched{};
    for (std::uint32_t scalar = 0U; scalar <= 0x10FFFFU; ++scalar) {
        if (scalar >= 0xD800U && scalar <= 0xDFFFU) {
            continue;
        }
        const auto codepoint = static_cast<codepoint_t>(scalar);
        const auto compact_result = compact::words_category(codepoint);
        const auto full_result = full::words_category(codepoint);
        if (compact_result != full_result) {
            FAIL() << "words_category differs at U+" << std::hex << scalar;
        }
        matched += compact_result != boundary_flag_t::none ? 1U : 0U;
    }
    EXPECT_EQ(matched, punctuation_codepoint_count);
}

TEST(UnicodeBackendIterateTest, MatchesOracleForEveryOneToThreeByteInput) {
    std::array<char, 3> bytes{};
    for (std::uint32_t length = 0U; length <= bytes.size(); ++length) {
        const auto cases = std::uint32_t{1U} << (length * 8U);
        for (std::uint32_t bits = 0U; bits < cases; ++bits) {
            auto remaining = bits;
            for (std::uint32_t index = 0U; index < length; ++index) {
                bytes[index] = static_cast<char>(remaining & 0xFFU);
                remaining >>= 8U;
            }
            expect_iterate_equal({bytes.data(), length});
        }
    }
}

TEST(UnicodeBackendIterateTest,
     MatchesOracleForFourByteStartersAndEveryFinalByte) {
    std::array<char, 4> bytes{};
    for (std::uint32_t lead = 0xF0U; lead <= 0xF7U; ++lead) {
        bytes[0] = static_cast<char>(lead);
        for (std::uint32_t prefix = 0U; prefix < (1U << 12U); ++prefix) {
            bytes[1] = static_cast<char>(0x80U | ((prefix >> 6U) & 0x3FU));
            bytes[2] = static_cast<char>(0x80U | (prefix & 0x3FU));
            for (std::uint32_t final = 0U; final <= 0xFFU; ++final) {
                bytes[3] = static_cast<char>(final);
                expect_iterate_equal({bytes.data(), bytes.size()});
            }
        }
    }
}

TEST(UnicodeBackendEncodeTest, MatchesOracleForEveryUnicodeScalar) {
    std::array<byte_t, 4> compact_bytes{};
    std::array<byte_t, 4> full_bytes{};
    for (std::uint32_t scalar = 0U; scalar <= 0x10FFFFU; ++scalar) {
        if (scalar >= 0xD800U && scalar <= 0xDFFFU) {
            continue;
        }
        const auto codepoint = static_cast<codepoint_t>(scalar);
        compact_bytes.fill(0U);
        full_bytes.fill(0U);
        const auto compact_size =
            compact::encode_char(codepoint, compact_bytes.data());
        const auto full_size = full::encode_char(codepoint, full_bytes.data());
        if (compact_size != full_size || compact_bytes != full_bytes) {
            FAIL() << "encode_char differs at U+" << std::hex << scalar;
        }
    }
}

} // namespace
} // namespace words::poc::unicode_backend
