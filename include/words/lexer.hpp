#pragma once

#include "words/lifetime.hpp"
#include "words/model.hpp"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

namespace words {

struct LexError final {
    DiagnosticCode code{DiagnosticCode::invalid_utf8};
    std::string message;
};

enum class BoundaryFlag : std::uint16_t {
    none = 0,
    whitespace = 1U << 0U,
    comma = 1U << 1U,
    semicolon = 1U << 2U,
    colon = 1U << 3U,
    period = 1U << 4U,
    question = 1U << 5U,
    exclamation = 1U << 6U,
    quote = 1U << 7U,
    apostrophe = 1U << 8U,
    dash = 1U << 9U,
    bracket = 1U << 10U,
    other_punctuation = 1U << 11U,
};

template <typename Enum> inline constexpr bool enable_enum_flags = false;

template <> inline constexpr bool enable_enum_flags<BoundaryFlag> = true;

template <typename Enum>
concept EnumFlags = std::is_scoped_enum_v<Enum> && enable_enum_flags<Enum>;

template <EnumFlags Enum>
[[nodiscard]] constexpr Enum operator|(const Enum left,
                                       const Enum right) noexcept {
    // A fixed-underlying scoped enum may hold any value representable by its
    // base type; the analyzer nevertheless treats flag combinations as if
    // they had to name an enumerator.
    // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange)
    return static_cast<Enum>(std::to_underlying(left) |
                             std::to_underlying(right));
}

template <EnumFlags Enum>
[[nodiscard]] constexpr Enum operator&(const Enum left,
                                       const Enum right) noexcept {
    return static_cast<Enum>(std::to_underlying(left) &
                             std::to_underlying(right));
}

template <EnumFlags Enum>
constexpr Enum &operator|=(Enum &left WORDS_LIFETIMEBOUND,
                           const Enum right) noexcept {
    left = left | right;
    return left;
}

template <EnumFlags Enum>
[[nodiscard]] constexpr bool has_any_flag(const Enum value,
                                          const Enum flags) noexcept {
    return std::to_underlying(value & flags) != 0;
}

struct TextBoundary final {
    BoundaryFlag flags{BoundaryFlag::none};
    std::size_t byte_begin{};
    std::size_t byte_end{};
};

struct TextToken final {
    std::string_view text;
    std::size_t byte_begin{};
    std::size_t byte_end{};
    TextBoundary boundary_after;
};

class TextTokenCursor final {
  public:
    explicit TextTokenCursor(std::string_view utf8 WORDS_LIFETIMEBOUND) noexcept
        : input_{utf8} {}

    [[nodiscard]] const TextToken *peek() noexcept WORDS_LIFETIMEBOUND;
    [[nodiscard]] std::optional<TextToken> next() noexcept;

  private:
    struct CachedToken final {
        TextToken token;
        std::size_t next_cursor{};
    };

    [[nodiscard]] std::optional<CachedToken>
    scan(std::size_t byte_offset) const noexcept;

    std::string_view input_;
    std::size_t cursor_{};
    std::optional<CachedToken> peeked_;
};

class LatinLexer final {
  public:
    [[nodiscard]] std::expected<SurfaceForm, LexError>
    lex(std::string_view utf8) const;
};

} // namespace words
