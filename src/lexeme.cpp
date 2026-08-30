#include "words/lexeme.hpp"

#include <cstddef>
#include <string>
#include <string_view>
#include <utility>

namespace words {
namespace {

[[nodiscard]] std::string stem(const Database &database,
                               const LexemeRecord &lexeme,
                               const std::size_t slot) {
    return std::string{database.stem_string(lexeme.stems.at(slot))};
}

[[nodiscard]] std::string add(const Database &database,
                              const LexemeRecord &lexeme,
                              const std::size_t slot,
                              const std::string_view ending) {
    auto result = stem(database, lexeme, slot);
    result.append(ending);
    return result;
}

[[nodiscard]] std::string noun_lemma(const Database &database,
                                     const LexemeRecord &lexeme) {
    switch (lexeme.declension) {
    case 1:
        switch (lexeme.variant) {
        case 1:
            return add(database, lexeme, 0U, "a");
        case 6:
            return add(database, lexeme, 0U, "e");
        case 7:
            return add(database, lexeme, 0U, "es");
        case 8:
            return add(database, lexeme, 0U, "as");
        default:
            break;
        }
        break;
    case 2:
        switch (lexeme.variant) {
        case 1:
        case 4:
        case 5:
        case 9:
            return add(database, lexeme, 0U,
                       lexeme.variant == 4U && lexeme.gender == Gender::neuter
                           ? "um"
                           : "us");
        case 2:
            return add(database, lexeme, 0U, "um");
        case 3:
            return stem(database, lexeme, 0U);
        case 6:
        case 7:
            return add(database, lexeme, 0U, "os");
        case 8:
            return add(database, lexeme, 0U, "on");
        default:
            break;
        }
        break;
    case 3:
        return stem(database, lexeme, 0U);
    case 4:
        return add(database, lexeme, 0U, lexeme.variant == 2U ? "u" : "us");
    case 5:
        return add(database, lexeme, 0U, "es");
    case 9:
        return add(database, lexeme, 0U, lexeme.variant == 8U ? "." : "");
    default:
        break;
    }
    return {};
}

[[nodiscard]] std::string adjective_lemma(const Database &database,
                                          const LexemeRecord &lexeme) {
    if (lexeme.adjective_degree == Degree::comparative) {
        return add(database, lexeme, 0U, "or");
    }
    if (lexeme.adjective_degree == Degree::superlative) {
        return add(database, lexeme, 0U, "mus");
    }
    if (lexeme.declension == 1U) {
        return lexeme.variant == 2U || lexeme.variant == 4U
                   ? stem(database, lexeme, 0U)
                   : add(database, lexeme, 0U, "us");
    }
    if (lexeme.declension == 2U) {
        switch (lexeme.variant) {
        case 1:
        case 2:
        case 8:
            return "-";
        case 3:
            return add(database, lexeme, 0U, "es");
        case 6:
        case 7:
            return add(database, lexeme, 0U, "os");
        default:
            break;
        }
    }
    if (lexeme.declension == 3U) {
        return lexeme.variant == 2U ? add(database, lexeme, 0U, "is")
                                    : stem(database, lexeme, 0U);
    }
    if (lexeme.declension == 9U) {
        return add(database, lexeme, 0U, lexeme.variant == 8U ? "." : "");
    }
    return {};
}

[[nodiscard]] std::string pronoun_lemma(const Database &database,
                                        const LexemeRecord &lexeme) {
    if (lexeme.part_of_speech == PartOfSpeech::pack) {
        return stem(database, lexeme, 0U);
    }
    switch (lexeme.declension) {
    case 3:
        return add(database, lexeme, 0U, "ic");
    case 4:
        return add(database, lexeme, 0U,
                   lexeme.variant == 2U ? "dem" : "s");
    case 6:
        return add(database, lexeme, 0U, "e");
    case 9:
        return add(database, lexeme, 0U, lexeme.variant == 8U ? "." : "");
    default:
        return {};
    }
}

[[nodiscard]] std::string numeral_lemma(const Database &database,
                                        const LexemeRecord &lexeme) {
    if (lexeme.numeral_type == NumeralType::unknown) {
        if (lexeme.declension == 1U) {
            constexpr std::string_view endings[]{"", "us", "o", "es", "i"};
            return lexeme.variant < std::size(endings)
                       ? add(database, lexeme, 0U, endings[lexeme.variant])
                       : std::string{};
        }
        return stem(database, lexeme, 0U);
    }
    if (lexeme.numeral_type == NumeralType::cardinal &&
        lexeme.declension == 1U) {
        constexpr std::string_view endings[]{"", "us", "o", "es", "i"};
        return lexeme.variant < std::size(endings)
                   ? add(database, lexeme, 0U, endings[lexeme.variant])
                   : std::string{};
    }
    if (lexeme.numeral_type == NumeralType::ordinal) {
        return add(database, lexeme, 0U, "us");
    }
    if (lexeme.numeral_type == NumeralType::distributive) {
        return add(database, lexeme, 0U, "i");
    }
    return stem(database, lexeme, 0U);
}

[[nodiscard]] std::string verb_lemma(const Database &database,
                                     const LexemeRecord &lexeme) {
    if (lexeme.verb_kind == VerbKind::perfect_definite) {
        return add(database, lexeme, 2U, "i");
    }
    if (lexeme.verb_kind == VerbKind::deponent) {
        return add(database, lexeme, 0U, lexeme.declension == 2U ? "eor" : "or");
    }
    if (lexeme.declension == 2U) {
        return add(database, lexeme, 0U, "eo");
    }
    if (lexeme.declension == 5U) {
        return add(database, lexeme, 0U, "um");
    }
    if (lexeme.declension == 7U && lexeme.variant == 2U) {
        return add(database, lexeme, 0U, "am");
    }
    if (lexeme.declension == 9U && lexeme.variant == 8U) {
        return add(database, lexeme, 0U, ".");
    }
    if (lexeme.declension == 9U && lexeme.variant == 9U) {
        return stem(database, lexeme, 0U);
    }
    return add(database, lexeme, 0U, "o");
}

} // namespace

std::string citation_lemma(const Database &database,
                           const LexemeRecord &lexeme,
                           const std::string_view fallback) {
    if (lexeme.dictionary == DictionaryKind::unique) {
        const auto direct = stem(database, lexeme, 0U);
        return direct.empty() ? std::string{fallback} : direct;
    }
    std::string result;
    switch (std::to_underlying(lexeme.part_of_speech)) {
    case std::to_underlying(PartOfSpeech::noun):
        result = noun_lemma(database, lexeme);
        break;
    case std::to_underlying(PartOfSpeech::pronoun):
    case std::to_underlying(PartOfSpeech::pack):
        result = pronoun_lemma(database, lexeme);
        break;
    case std::to_underlying(PartOfSpeech::adjective):
        result = adjective_lemma(database, lexeme);
        break;
    case std::to_underlying(PartOfSpeech::numeral):
        result = numeral_lemma(database, lexeme);
        break;
    case std::to_underlying(PartOfSpeech::verb):
        result = verb_lemma(database, lexeme);
        break;
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
        result = stem(database, lexeme, 0U);
        break;
    default:
        result = stem(database, lexeme, 0U);
        break;
    }
    // WHY: malformed or intentionally exceptional legacy paradigms must still
    // produce a useful lookup key instead of an empty Pagefind query.
    return result.empty() ? std::string{fallback} : result;
}

} // namespace words
