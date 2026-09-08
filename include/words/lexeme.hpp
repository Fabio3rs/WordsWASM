#pragma once

#include "words/database.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

namespace words {

struct DeclensionParadigm final {
    std::uint8_t number{};
    std::uint8_t variant{};
};

struct ConjugationParadigm final {
    std::uint8_t number{};
    std::uint8_t variant{};
};

using LexemeParadigm =
    std::variant<std::monostate, DeclensionParadigm, ConjugationParadigm>;

[[nodiscard]] constexpr LexemeParadigm
lexeme_paradigm(const LexemeRecord &lexeme) noexcept {
    switch (std::to_underlying(lexeme.part_of_speech)) {
    case std::to_underlying(PartOfSpeech::noun):
    case std::to_underlying(PartOfSpeech::pronoun):
    case std::to_underlying(PartOfSpeech::pack):
    case std::to_underlying(PartOfSpeech::adjective):
    case std::to_underlying(PartOfSpeech::numeral):
        return DeclensionParadigm{.number = lexeme.declension,
                                  .variant = lexeme.variant};
    case std::to_underlying(PartOfSpeech::verb):
        return ConjugationParadigm{.number = lexeme.declension,
                                   .variant = lexeme.variant};
    case std::to_underlying(PartOfSpeech::unknown):
    case std::to_underlying(PartOfSpeech::adverb):
    case std::to_underlying(PartOfSpeech::participle):
    case std::to_underlying(PartOfSpeech::supine):
    case std::to_underlying(PartOfSpeech::preposition):
    case std::to_underlying(PartOfSpeech::conjunction):
    case std::to_underlying(PartOfSpeech::interjection):
    case std::to_underlying(PartOfSpeech::tackon):
    case std::to_underlying(PartOfSpeech::prefix):
    case std::to_underlying(PartOfSpeech::suffix):
        return std::monostate{};
    default:
        return std::monostate{};
    }
}

// WHY: the canonical headword depends only on compact stems and lexical flags.
// Keeping this formatter independent of meanings lets search-only clients
// resolve a persistent LexemeId without loading the editorial definition pool.
[[nodiscard]] std::string citation_lemma(const Database &database,
                                         const LexemeRecord &lexeme,
                                         std::string_view fallback = {});

// Whitaker's dictionary-form display is a lexical domain operation.  Keeping
// it beside citation_lemma prevents presentation backends from interpreting
// raw declension/conjugation and variant bytes independently.
[[nodiscard]] std::string dictionary_form(const Database &database,
                                          const LexemeRecord &lexeme,
                                          std::string_view fallback = {});

} // namespace words
