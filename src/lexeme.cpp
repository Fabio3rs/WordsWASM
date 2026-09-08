#include "words/lexeme.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
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

constexpr std::string_view missing_stem_prefix{"zzz"};
constexpr auto numeral_lemma_endings =
    std::to_array<std::string_view>({"", "us", "o", "es", "i"});

[[nodiscard]] constexpr std::string_view
additional_verb_infinitive_ending(const std::uint8_t variant) noexcept {
    switch (variant) {
    case 1:
        return "are";
    case 4:
        return "ire";
    default:
        return "ere";
    }
}

// Whitaker's Decn_Record is explicitly a pair of numeric Which/Variant codes,
// shared by declensions and conjugations rather than a closed Ada enum. These
// tables are the domain translation boundary for those wire-compatible codes;
// inventing semantic enumerator names for context-dependent variants would be
// misleading.
// NOLINTBEGIN(cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers)
[[nodiscard]] std::string
noun_dictionary_form(const Database &database, const LexemeRecord &lexeme,
                     const std::string_view fallback) {
    const auto stem = [&](const std::size_t slot) {
        return std::string{database.stem_string(lexeme.stems.at(slot))};
    };
    const auto add = [&](const std::size_t slot,
                         const std::string_view ending) {
        return stem(slot) + std::string{ending};
    };

    std::array<std::string, 2> forms;
    switch (lexeme.declension) {
    case 1:
        switch (lexeme.variant) {
        case 1:
            forms = {add(0, "a"), add(1, "ae")};
            break;
        case 6:
            forms = {add(0, "e"), add(1, "es")};
            break;
        case 7:
            forms = {add(0, "es"), add(1, "ae")};
            break;
        case 8:
            forms = {add(0, "as"), add(1, "ae")};
            break;
        default:
            break;
        }
        break;
    case 2:
        switch (lexeme.variant) {
        case 1:
            forms = {add(0, "us"), add(1, "i")};
            break;
        case 2:
            forms = {add(0, "um"), add(1, "i")};
            break;
        case 3:
            forms = {add(0, ""), add(1, "i")};
            break;
        case 4:
            forms = {
                add(0, lexeme.gender == Gender::neuter ? "um" : "us"),
                add(1, "(i)"),
            };
            break;
        case 5:
            forms = {add(0, "us"), add(1, "")};
            break;
        case 6:
        case 7:
            forms = {add(0, "os"), add(1, "i")};
            break;
        case 8:
            forms = {add(0, "on"), add(1, "i")};
            break;
        case 9:
            forms = {add(0, "us"), add(1, "i")};
            break;
        default:
            break;
        }
        break;
    case 3:
        forms = {
            add(0, ""),
            add(1,
                lexeme.variant == 7U || lexeme.variant == 9U ? "os/is" : "is"),
        };
        break;
    case 4:
        switch (lexeme.variant) {
        case 1:
            forms = {add(0, "us"), add(1, "us")};
            break;
        case 2:
            forms = {add(0, "u"), add(1, "us")};
            break;
        case 3:
            forms = {add(0, "us"), add(1, "u")};
            break;
        default:
            break;
        }
        break;
    case 5:
        forms = {add(0, "es"), add(1, "ei")};
        break;
    case 9:
        if (lexeme.variant == 8U) {
            forms = {add(0, "."), "abb."};
        } else if (lexeme.variant == 9U) {
            forms = {add(0, ""), "undeclined"};
        }
        break;
    default:
        break;
    }

    if (forms[0].empty()) {
        return fallback.empty() ? stem(0) : std::string{fallback};
    }
    if (forms[1].empty()) {
        return forms[0];
    }
    return forms[0] + ", " + forms[1];
}

[[nodiscard]] std::string
adjective_dictionary_form(const Database &database, const LexemeRecord &lexeme,
                          const std::string_view fallback) {
    const auto stem = [&](const std::size_t slot) {
        return std::string{database.stem_string(lexeme.stems.at(slot))};
    };
    const auto add = [&](const std::size_t slot,
                         const std::string_view ending) {
        return stem(slot) + std::string{ending};
    };

    std::array<std::string, 4> forms;
    if (lexeme.adjective_degree == Degree::comparative) {
        forms = {add(0, "or"), add(0, "or"), add(0, "us"), {}};
    } else if (lexeme.adjective_degree == Degree::superlative) {
        forms = {add(0, "mus"), add(0, "ma"), add(0, "mum"), {}};
    } else if (lexeme.adjective_degree == Degree::positive) {
        if (lexeme.declension == 1U) {
            switch (lexeme.variant) {
            case 1:
                forms = {add(0, "us"), add(1, "a"), add(1, "um"), {}};
                break;
            case 2:
            case 4:
                forms = {add(0, ""), add(1, "a"), add(1, "um"), {}};
                break;
            case 3:
                forms = {
                    add(0, "us"), add(1, "a"), add(1, "um (gen -ius)"), {}};
                break;
            case 5:
                forms = {add(0, "us"), add(1, "a"), add(1, "ud"), {}};
                break;
            default:
                break;
            }
        } else if (lexeme.declension == 2U) {
            switch (lexeme.variant) {
            case 1:
                forms = {"-", add(0, "e"), "-", {}};
                break;
            case 2:
                forms = {"-", "a", "-", {}};
                break;
            case 3:
                forms = {add(0, "es"), add(0, "es"), add(0, "es"), {}};
                break;
            case 6:
                forms = {add(0, "os"), add(0, "os"), "-", {}};
                break;
            case 7:
                forms = {add(0, "os"), "-", "-", {}};
                break;
            case 8:
                forms = {"-", "-", add(1, "on"), {}};
                break;
            default:
                break;
            }
        } else if (lexeme.declension == 3U) {
            switch (lexeme.variant) {
            case 1:
                forms = {add(0, ""), "(gen.)", add(1, "is"), {}};
                break;
            case 2:
                forms = {add(0, "is"), add(1, "is"), add(1, "e"), {}};
                break;
            case 3:
                forms = {add(0, ""), add(1, "is"), add(1, "e"), {}};
                break;
            case 6:
                forms = {add(0, ""), "(gen.)", add(1, "os"), {}};
                break;
            default:
                break;
            }
        } else if (lexeme.declension == 9U && lexeme.variant == 8U) {
            forms = {add(0, "."), "abb.", {}, {}};
        } else if (lexeme.declension == 9U && lexeme.variant == 9U) {
            forms = {add(0, ""), "undeclined", {}, {}};
        }
    } else {
        if (lexeme.declension == 1U && lexeme.variant == 1U) {
            forms = {add(0, "us"), add(1, "a -um"), add(2, "or -or -us"),
                     add(3, "mus -a -um")};
        } else if (lexeme.declension == 1U && lexeme.variant == 2U) {
            forms = {add(0, ""), add(1, "a -um"), add(2, "or -or -us"),
                     add(3, "mus -a -um")};
        } else if (lexeme.declension == 3U) {
            switch (lexeme.variant) {
            case 1:
                forms = {add(0, ""), add(1, "is (gen.)"), add(2, "or -or -us"),
                         add(3, "mus -a -um")};
                break;
            case 2:
                forms = {add(0, "is"), add(1, "e"), add(2, "or -or -us"),
                         add(3, "mus -a -um")};
                break;
            case 3:
                forms = {add(0, ""), add(1, "is -e"), add(2, "or -or -us"),
                         add(3, "mus -a -um")};
                break;
            default:
                break;
            }
        } else if (lexeme.declension == 9U) {
            forms = {add(0, ""), "undeclined", add(2, "or -or -us"),
                     add(3, "mus -a -um")};
        }
    }

    std::string result;
    for (auto &form : forms) {
        if (form.starts_with(missing_stem_prefix)) {
            form = "-";
        }
        if (form.empty()) {
            continue;
        }
        if (!result.empty()) {
            result.append(", ");
        }
        result.append(form);
    }
    if (!result.empty()) {
        return result;
    }

    // Fix not eliding copy on return
    result = fallback.empty() ? stem(0) : std::string{fallback};

    return result;
}

[[nodiscard]] std::string
pronoun_dictionary_form(const Database &database, const LexemeRecord &lexeme,
                        const std::string_view fallback) {
    const auto stem = [&](const std::size_t slot) {
        return std::string{database.stem_string(lexeme.stems.at(slot))};
    };
    const auto add = [&](const std::size_t slot,
                         const std::string_view ending) {
        return stem(slot) + std::string{ending};
    };
    if (lexeme.part_of_speech == PartOfSpeech::pack) {
        return stem(0);
    }

    std::array<std::string, 3> forms;
    switch (lexeme.declension) {
    case 3:
        forms = {add(0, "ic"), add(0, "aec"),
                 add(0, lexeme.variant == 2U ? "uc" : "oc")};
        break;
    case 4:
        if (lexeme.variant == 1U) {
            forms = {add(0, "s"), add(1, "a"), add(0, "d")};
        } else if (lexeme.variant == 2U) {
            forms = {add(0, "dem"), add(1, "adem"), add(0, "dem")};
        }
        break;
    case 6:
        forms = {add(0, "e"), add(0, "a"),
                 add(0, lexeme.variant == 2U ? "um" : "ud")};
        break;
    case 9:
        if (lexeme.variant == 8U) {
            forms = {add(0, "."), "abb.", {}};
        } else if (lexeme.variant == 9U) {
            forms = {add(0, ""), "undeclined", {}};
        }
        break;
    default:
        break;
    }
    std::string result;
    for (const auto &form : forms) {
        if (form.empty()) {
            continue;
        }
        if (!result.empty()) {
            result.append(", ");
        }
        result.append(form);
    }
    return result.empty() ? std::string{fallback} : result;
}

[[nodiscard]] std::string
join_dictionary_forms(std::array<std::string, 4> forms,
                      const std::string_view fallback) {
    std::string result;
    for (auto &form : forms) {
        if (form.starts_with(missing_stem_prefix)) {
            form = "-";
        }
        if (form.empty()) {
            continue;
        }
        if (!result.empty()) {
            result.append(", ");
        }
        result.append(form);
    }
    return result.empty() ? std::string{fallback} : result;
}

[[nodiscard]] std::string
numeral_dictionary_form(const Database &database, const LexemeRecord &lexeme,
                        const std::string_view fallback) {
    const auto stem = [&](const std::size_t slot) {
        return std::string{database.stem_string(lexeme.stems.at(slot))};
    };
    const auto add = [&](const std::size_t slot,
                         const std::string_view ending) {
        return stem(slot) + std::string{ending};
    };
    std::array<std::string, 4> forms;
    if (lexeme.numeral_type == NumeralType::unknown) {
        if (lexeme.declension == 1U) {
            switch (lexeme.variant) {
            case 1:
                forms = {add(0, "us -a -um"), add(1, "us -a -um"),
                         add(2, "i -ae -a"), add(3, "")};
                break;
            case 2:
                forms = {add(0, "o -ae o"), add(1, "us -a -um"),
                         add(2, "i -ae -a"), add(3, "")};
                break;
            case 3:
                forms = {add(0, "es -es -ia"), add(1, "us -a -um"),
                         add(2, "i -ae -a"), add(3, "")};
                break;
            case 4:
                forms = {add(0, "i -ae -a"), add(1, "us -a -um"),
                         add(2, "i -ae -a"), add(3, "ie (n)s")};
                break;
            default:
                break;
            }
        } else if (lexeme.declension == 2U) {
            forms = {add(0, ""), add(1, "us -a -um"), add(2, "i -ae -a"),
                     add(3, "ie (n)s")};
        }
    } else if (lexeme.numeral_type == NumeralType::cardinal) {
        if (lexeme.declension == 1U) {
            switch (lexeme.variant) {
            case 1:
                forms = {add(0, "us"), add(0, "a"), add(0, "um"), {}};
                break;
            case 2:
                forms = {add(0, "o"), add(0, "ae"), add(0, "o"), {}};
                break;
            case 3:
                forms = {add(0, "es"), add(0, "es"), add(0, "ia"), {}};
                break;
            case 4:
                forms = {add(0, "i"), add(0, "ae"), add(0, "a"), {}};
                break;
            default:
                break;
            }
        } else if (lexeme.declension == 2U) {
            forms[0] = add(0, "");
        }
    } else if (lexeme.numeral_type == NumeralType::ordinal) {
        forms = {add(0, "us"), add(0, "a"), add(0, "um"), {}};
    } else if (lexeme.numeral_type == NumeralType::distributive) {
        forms = {add(0, "i"), add(0, "ae"), add(0, "a"), {}};
    } else {
        forms[0] = add(0, "");
    }
    return join_dictionary_forms(std::move(forms), fallback);
}

[[nodiscard]] std::string
adverb_dictionary_form(const Database &database, const LexemeRecord &lexeme,
                       const std::string_view fallback) {
    std::array<std::string, 4> forms;
    const auto count = lexeme.adverb_degree == Degree::unknown ? 3U : 1U;
    for (std::size_t slot = 0; slot < count; ++slot) {
        forms.at(slot) =
            std::string{database.stem_string(lexeme.stems.at(slot))};
    }
    return join_dictionary_forms(std::move(forms), fallback);
}

[[nodiscard]] std::string
verb_dictionary_form(const Database &database, const LexemeRecord &lexeme,
                     const std::string_view fallback) {
    const auto stem = [&](const std::size_t slot) {
        return std::string{database.stem_string(lexeme.stems.at(slot))};
    };
    const auto add = [&](const std::size_t slot,
                         const std::string_view ending) {
        return stem(slot) + std::string{ending};
    };
    std::array<std::string, 4> forms;
    if (lexeme.verb_kind == VerbKind::deponent) {
        forms[3] = add(3, "us sum");
        switch (lexeme.declension) {
        case 1:
            forms[0] = add(0, "or");
            forms[1] = add(1, "ari");
            break;
        case 2:
            forms[0] = add(0, "eor");
            forms[1] = add(1, "eri");
            break;
        case 3:
            forms[0] = add(0, "or");
            forms[1] = add(1, lexeme.variant == 4U ? "iri" : "i");
            break;
        default:
            break;
        }
        return join_dictionary_forms(std::move(forms), fallback);
    }
    if (lexeme.verb_kind == VerbKind::perfect_definite) {
        forms = {add(2, "i"), add(2, "isse"), add(3, "us"), {}};
        return join_dictionary_forms(std::move(forms), fallback);
    }

    if (lexeme.declension == 2U) {
        forms[0] = add(0, "eo");
    } else if (lexeme.declension == 5U) {
        forms[0] = add(0, "um");
    } else if (lexeme.declension == 7U && lexeme.variant == 2U) {
        forms[0] = add(0, "am");
    } else {
        forms[0] = add(0, "o");
    }

    switch (lexeme.declension) {
    case 1:
        forms[1] = add(1, "are");
        break;
    case 2:
        forms[1] = add(1, "ere");
        break;
    case 3:
        switch (lexeme.variant) {
        case 2:
            forms[1] = add(1, "re");
            break;
        case 3:
            forms[1] = stem(1) == "f" ? add(1, "ieri") : add(1, "eri");
            break;
        case 4:
            forms[1] = add(1, "ire");
            break;
        default:
            forms[1] = add(1, "ere");
            break;
        }
        break;
    case 5:
        forms[1] = lexeme.variant == 1U ? add(1, "esse") : add(0, "e");
        break;
    case 6:
        forms[1] = add(1, lexeme.variant == 2U ? "le" : "re");
        break;
    case 7:
        if (lexeme.variant == 3U) {
            forms[1] = add(1, "se");
        }
        break;
    case 8:
        forms[1] = add(1, additional_verb_infinitive_ending(lexeme.variant));
        break;
    case 9:
        if (lexeme.variant == 8U) {
            forms = {add(0, "."), "abb.", {}, {}};
        } else if (lexeme.variant == 9U) {
            forms = {add(0, ""), "undeclined", {}, {}};
        }
        return join_dictionary_forms(std::move(forms), fallback);
    default:
        break;
    }

    if (lexeme.verb_kind == VerbKind::impersonal) {
        forms[2] = add(2, "it");
        forms[3] = add(3, "us est");
    } else if (lexeme.verb_kind == VerbKind::semideponent) {
        forms[2] = add(2, "i");
        forms[3] = add(3, "us sum");
    } else if (lexeme.declension == 5U && lexeme.variant == 1U) {
        forms[2] = add(2, "i");
        forms[3] = add(3, "urus");
    } else if (lexeme.declension == 8U) {
        forms[2] = "additional";
        forms[3] = "forms";
    } else {
        forms[2] = add(2, "i");
        forms[3] = add(3, "us");
    }
    if (lexeme.declension == 6U && lexeme.variant == 1U) {
        forms[2].append("(ii)");
    }
    return join_dictionary_forms(std::move(forms), fallback);
}

[[nodiscard]] std::string
simple_dictionary_form(const Database &database, const LexemeRecord &lexeme,
                       const std::string_view fallback) {
    const auto stem = database.stem_string(lexeme.stems.front());

    return stem.empty() ? std::string{fallback} : std::string{stem};
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
        return add(database, lexeme, 0U, lexeme.variant == 2U ? "dem" : "s");
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
            return lexeme.variant < numeral_lemma_endings.size()
                       ? add(database, lexeme, 0U,
                             numeral_lemma_endings.at(lexeme.variant))
                       : std::string{};
        }
        return stem(database, lexeme, 0U);
    }
    if (lexeme.numeral_type == NumeralType::cardinal &&
        lexeme.declension == 1U) {
        return lexeme.variant < numeral_lemma_endings.size()
                   ? add(database, lexeme, 0U,
                         numeral_lemma_endings.at(lexeme.variant))
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
        return add(database, lexeme, 0U,
                   lexeme.declension == 2U ? "eor" : "or");
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
// NOLINTEND(cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers)

} // namespace

std::string citation_lemma(const Database &database, const LexemeRecord &lexeme,
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
    default:
        result = stem(database, lexeme, 0U);
        break;
    }
    // WHY: malformed or intentionally exceptional legacy paradigms must still
    // produce a useful lookup key instead of an empty Pagefind query.
    return result.empty() ? std::string{fallback} : result;
}

std::string dictionary_form(const Database &database,
                            const LexemeRecord &lexeme,
                            const std::string_view fallback) {
    if (lexeme.dictionary == DictionaryKind::unique) {
        return simple_dictionary_form(database, lexeme, fallback);
    }
    switch (std::to_underlying(lexeme.part_of_speech)) {
    case std::to_underlying(PartOfSpeech::noun):
        return noun_dictionary_form(database, lexeme, fallback);
    case std::to_underlying(PartOfSpeech::pronoun):
    case std::to_underlying(PartOfSpeech::pack):
        return pronoun_dictionary_form(database, lexeme, fallback);
    case std::to_underlying(PartOfSpeech::adjective):
        return adjective_dictionary_form(database, lexeme, fallback);
    case std::to_underlying(PartOfSpeech::numeral):
        return numeral_dictionary_form(database, lexeme, fallback);
    case std::to_underlying(PartOfSpeech::adverb):
        return adverb_dictionary_form(database, lexeme, fallback);
    case std::to_underlying(PartOfSpeech::verb):
        return verb_dictionary_form(database, lexeme, fallback);
    default:
        return simple_dictionary_form(database, lexeme, fallback);
    }
}

} // namespace words
