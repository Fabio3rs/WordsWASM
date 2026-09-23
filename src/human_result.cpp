#include "human_result.hpp"

#include "words/lexeme.hpp"
#include "words/projection.hpp"
#include "words/semantics.hpp"

#include <algorithm>
#include <cctype>
#include <optional>
#include <ranges>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace words::client {
namespace {

struct Reading final {
    std::string display;
    std::string quantity_coverage{"none"};
    std::string annotated;
    std::string lemma;
    std::string dictionary;
    std::string part;
    std::string features;
    std::string meaning;
    std::string note;
    std::string explanation;
    std::string metadata;
};

struct Unit final {
    std::string name;
    std::string input;
    std::string status;
    std::vector<Reading> readings;
    bool filtered_all{};
    std::string diagnostic;
    std::string split_component;
    std::string enclitic;
};

struct SuggestedEnclitic final {
    std::string base;
    std::string suffix;
};

[[nodiscard]] std::optional<SuggestedEnclitic>
suggested_enclitic(const Database &database, const WordSegmentIR &segment) {
    if (segment.analyses.empty())
        return std::nullopt;

    std::string_view shared_suffix;
    for (const auto &analysis : segment.analyses) {
        std::string_view suffix;
        for (const auto addon_id : analysis.derivation.steps()) {
            if (database.addon_kind(addon_id) != AddonKind::tackon)
                continue;
            const auto &tackon = database.tackon(addon_id);
            if (tackon.enclitic)
                suffix = database.tackon_string(tackon.fix);
        }
        if (suffix.empty() ||
            (!shared_suffix.empty() && suffix != shared_suffix))
            return std::nullopt;
        shared_suffix = suffix;
    }
    if (shared_suffix.empty() ||
        !std::string_view{segment.surface.lookup_ascii}.ends_with(
            shared_suffix) ||
        segment.surface.normalized_nfc.size() <= shared_suffix.size())
        return std::nullopt;

    return SuggestedEnclitic{
        .base = segment.surface.normalized_nfc.substr(
            0U, segment.surface.normalized_nfc.size() - shared_suffix.size()),
        .suffix = std::string{shared_suffix}};
}

void add(std::string &target, const std::string_view value) {
    if (value.empty())
        return;
    if (!target.empty())
        target.append(" · ");
    target.append(value);
}

void add_word(std::string &target, const std::string_view value) {
    if (value.empty())
        return;
    if (!target.empty())
        target.push_back(' ');
    target.append(value);
}

void add_ordinal(std::string &target, const unsigned value,
                 const std::string_view label) {
    if (value == 0U)
        return;
    const auto suffix = value % 100U >= 11U && value % 100U <= 13U ? "th"
                        : value % 10U == 1U                        ? "st"
                        : value % 10U == 2U                        ? "nd"
                        : value % 10U == 3U                        ? "rd"
                                                                   : "th";
    add(target, std::to_string(value) + suffix + " " + std::string{label});
}

[[nodiscard]] std::string safe_text(const std::string_view value) {
    std::string output;
    output.reserve(value.size());
    constexpr char hex[] = "0123456789ABCDEF";
    for (const char character : value) {
        const auto byte = static_cast<unsigned char>(character);
        if (byte == '\\')
            output.append("\\\\");
        else if (byte == '\t')
            output.append("\\t");
        else if (byte == '\n')
            output.append("\\n");
        else if (byte == '\r')
            output.append("\\r");
        else if (byte < 0x20U || byte == 0x7FU) {
            output.append("\\x");
            output.push_back(hex[byte >> 4U]);
            output.push_back(hex[byte & 0xFU]);
        } else
            output.push_back(static_cast<char>(byte));
    }
    return output;
}

[[nodiscard]] std::string readable_name(const std::string_view value,
                                        const bool title = false) {
    std::string output;
    bool capitalize = title;
    for (const char character : value) {
        if (character == '-' || character == '_') {
            output.push_back(' ');
            capitalize = title;
        } else if (capitalize) {
            output.push_back(static_cast<char>(
                std::toupper(static_cast<unsigned char>(character))));
            capitalize = false;
        } else
            output.push_back(character);
    }
    return output;
}

[[nodiscard]] std::string lexical_metadata(const LexemeRecord &lexeme,
                                           const InflectionRule *rule) {
    std::string result;
    const auto append = [&](const std::string_view name,
                            const std::string_view value,
                            const bool title = false) {
        if (value.empty())
            return;
        if (!result.empty())
            result.append("; ");
        result.append(name);
        result.append(": ");
        result.append(readable_name(value, title));
    };
    append("age", age_name(lexeme.age));
    append("subject", subject_name(lexeme.subject));
    append("region", geography_name(lexeme.geography));
    append("lexical frequency", lexical_frequency_name(lexeme.frequency));
    const auto source = source_name(lexeme.source);
    if (source != "source-a" && source != "source-u" && source != "temporary" &&
        source != "user-submitted")
        append("dictionary source", source, true);
    if (rule != nullptr)
        append("inflection frequency", rule_frequency_name(rule->frequency));
    return result;
}

[[nodiscard]] std::string person_number(const Person person,
                                        const GrammaticalNumber number) {
    if (person == Person::unknown)
        return {};
    std::string result;
    add_ordinal(result, static_cast<unsigned>(person), "person");
    add_word(result, number_name(number));
    return result;
}

[[nodiscard]] std::string features(const Morphology &morphology,
                                   const VerbKind kind) {
    std::string result;
    const bool deponent =
        kind == VerbKind::deponent || kind == VerbKind::semideponent;
    std::visit(
        [&](const auto &value) {
            using T = std::remove_cvref_t<decltype(value)>;
            if constexpr (std::is_same_v<T, NounMorphology> ||
                          std::is_same_v<T, PronounMorphology> ||
                          std::is_same_v<T, AdjectiveMorphology> ||
                          std::is_same_v<T, NumeralMorphology>) {
                if (value.declension <= 5U)
                    add_ordinal(result, value.declension, "declension");
                if constexpr (std::is_same_v<T, NumeralMorphology>)
                    add(result, numeral_type_name(value.numeral_type));
                if constexpr (std::is_same_v<T, AdjectiveMorphology>)
                    add(result, degree_name(value.degree));
                std::string inflection;
                add_word(inflection, case_name(value.grammatical_case));
                add_word(inflection, number_name(value.number));
                add_word(inflection, gender_name(value.gender));
                add(result, inflection);
            } else if constexpr (std::is_same_v<T, AdverbMorphology>) {
                add(result, degree_name(value.degree));
            } else if constexpr (std::is_same_v<T, VerbMorphology>) {
                if (value.conjugation <= 4U)
                    add_ordinal(result, value.conjugation, "conjugation");
                std::string grammar;
                add_word(grammar, tense_name(value.tense));
                add_word(grammar, mood_name(value.mood));
                if (!deponent)
                    add_word(grammar, voice_name(value.voice));
                add(result, grammar);
                add(result, person_number(value.person, value.number));
            } else if constexpr (std::is_same_v<T, ParticipleMorphology>) {
                if (value.conjugation <= 4U)
                    add_ordinal(result, value.conjugation, "conjugation");
                std::string grammar;
                add_word(grammar, tense_name(value.tense));
                if (value.tense == Tense::future &&
                    value.voice == Voice::passive)
                    add_word(grammar, "gerundive");
                else if (!deponent)
                    add_word(grammar, voice_name(value.voice));
                add(result, grammar);
                std::string inflection;
                add_word(inflection, case_name(value.grammatical_case));
                add_word(inflection, number_name(value.number));
                add_word(inflection, gender_name(value.gender));
                add(result, inflection);
            } else if constexpr (std::is_same_v<T, SupineMorphology>) {
                add(result, case_name(value.grammatical_case));
                add(result, number_name(value.number));
                add(result, gender_name(value.gender));
            } else if constexpr (std::is_same_v<T, PrepositionMorphology>) {
                if (value.governs != GrammaticalCase::unknown)
                    add(result, std::string{"governs "} +
                                    std::string{case_name(value.governs)});
            }
        },
        morphology);
    return result;
}

[[nodiscard]] std::string trim_note(const WhitakerTrimReason reason) {
    switch (std::to_underlying(reason)) {
    case std::to_underlying(WhitakerTrimReason::unsupported_short_imperative):
        return "short imperative outside Whitaker's listed forms";
    case std::to_underlying(WhitakerTrimReason::invalid_imperative_person):
        return "imperative person outside Whitaker's display policy";
    case std::to_underlying(WhitakerTrimReason::impersonal_non_third_person):
        return "non-third-person form of an impersonal verb";
    case std::to_underlying(WhitakerTrimReason::deponent_active_form):
        return "form outside Whitaker's usual deponent display";
    case std::to_underlying(
        WhitakerTrimReason::semideponent_passive_present_system):
        return "present-system form outside Whitaker's usual semideponent "
               "display";
    case std::to_underlying(
        WhitakerTrimReason::semideponent_active_perfect_system):
        return "perfect-system form outside Whitaker's usual semideponent "
               "display";
    default:
        return {};
    }
}

[[nodiscard]] std::string notice_note(const MorphologicalNotice notice) {
    switch (std::to_underlying(notice)) {
    case std::to_underlying(
        MorphologicalNotice::related_passive_usage_attested):
        return "related usage is attested; the queried form is not thereby "
               "attested";
    case std::to_underlying(MorphologicalNotice::source_disagreement):
        return "grammatical or lexical sources disagree with Whitaker's "
               "display policy";
    case std::to_underlying(MorphologicalNotice::manual_review_recommended):
        return "contextual review recommended";
    default:
        return {};
    }
}

[[nodiscard]] std::string diagnostic_text(const DiagnosticCode code) {
    switch (std::to_underlying(code)) {
    case std::to_underlying(DiagnosticCode::empty_input):
        return "empty input";
    case std::to_underlying(DiagnosticCode::input_too_large):
        return "input is too long";
    case std::to_underlying(DiagnosticCode::invalid_utf8):
        return "invalid UTF-8 text";
    case std::to_underlying(DiagnosticCode::invalid_vowel_quantity):
        return "invalid vowel quantity marks";
    case std::to_underlying(DiagnosticCode::unicode_normalization_failed):
        return "could not normalize the input";
    case std::to_underlying(DiagnosticCode::unsupported_character):
        return "unsupported character";
    case std::to_underlying(DiagnosticCode::unsupported_part_of_speech):
        return "unsupported part of speech";
    case std::to_underlying(DiagnosticCode::unsupported_token_count):
        return "too many tokens";
    case std::to_underlying(DiagnosticCode::unsupported_multi_token):
        return "unsupported multiword input";
    case std::to_underlying(DiagnosticCode::unknown_word):
        return "no reading found";
    case std::to_underlying(DiagnosticCode::two_words_suggestion):
        return "no confirmed reading; possible split below";
    default:
        return "analysis unavailable";
    }
}

[[nodiscard]] std::string
assessment_note(const MorphologicalAssessmentIR &assessment,
                const bool detailed) {
    std::string result;
    if (!assessment.whitaker_trim.accepted()) {
        result = "Whitaker would omit this reading";
        if (detailed) {
            for (const auto reason : assessment.whitaker_trim.values()) {
                result.append("; ");
                result.append(trim_note(reason));
            }
        }
    }
    if (!assessment.notice_values().empty()) {
        if (!result.empty())
            result.append("; ");
        result.append("editorial note");
        if (detailed) {
            for (const auto notice : assessment.notice_values()) {
                result.append("; ");
                result.append(notice_note(notice));
            }
        }
    }
    return result;
}

[[nodiscard]] std::string short_meaning(const std::string_view raw) {
    const auto meaning = normalized_meaning(raw);
    std::string output;
    std::size_t clauses{};
    for (const char character : meaning) {
        if (character == '[' || character == '\n' || character == '\r')
            break;
        if (character == ';') {
            ++clauses;
            if (clauses == 2U)
                break;
        }
        output.push_back(character);
    }
    while (!output.empty() && output.back() == ' ')
        output.pop_back();
    while (!output.empty() && output.back() == ';')
        output.pop_back();
    return output;
}

[[nodiscard]] Reading lexical_reading(const Database &database,
                                      const SurfaceForm &surface,
                                      const AnalysisIR &analysis,
                                      const HumanOptions options) {
    const auto &lexeme = database.lexeme(analysis.lexeme);
    const auto form = resolved_form(database, surface, analysis);
    Reading reading;
    reading.display = form.display;
    reading.quantity_coverage = quantity_coverage_name(form.quantity.coverage);
    reading.annotated = form.quantity.annotated.value_or("");
    reading.lemma = citation_lemma(database, lexeme, surface.normalized_nfc);
    reading.dictionary =
        dictionary_form(database, lexeme, surface.normalized_nfc);
    reading.part = std::string{
        morphology_part_name(analysis.morphology, lexeme.part_of_speech)};
    if (lexeme.verb_kind == VerbKind::deponent ||
        lexeme.verb_kind == VerbKind::semideponent) {
        add(reading.part, lexeme.verb_kind == VerbKind::deponent
                              ? (options.compact ? "dep" : "deponent")
                              : (options.compact ? "semidep" : "semideponent"));
    }
    reading.features = features(analysis.morphology, lexeme.verb_kind);
    reading.meaning = short_meaning(database.meaning(lexeme.meaning));
    reading.note = assessment_note(analysis.assessment, options.detailed);
    if (options.detailed) {
        reading.metadata = lexical_metadata(
            lexeme, analysis.rule ? &database.rule(*analysis.rule) : nullptr);
        if (const auto *participle =
                std::get_if<ParticipleMorphology>(&analysis.morphology);
            participle && participle->tense == Tense::future &&
            participle->voice == Voice::passive) {
            reading.explanation = "Gerundive: a future participial form "
                                  "expressing necessity or obligation.";
        } else if (lexeme.verb_kind == VerbKind::deponent) {
            reading.explanation = "Deponent verb: its verbal forms generally "
                                  "have active meaning.";
        } else if (lexeme.verb_kind == VerbKind::semideponent) {
            reading.explanation = "Semideponent verb: its present and perfect "
                                  "systems use different forms.";
        }
    }
    return reading;
}

[[nodiscard]] Reading compound_reading(const Database &database,
                                       const QueryResult &result,
                                       const CompoundAnalysisIR &analysis,
                                       const HumanOptions options) {
    const auto &lexeme = database.lexeme(analysis.lexeme);
    Reading reading;
    reading.display = result.multi_token_query
                          ? result.multi_token_query->normalized_nfc
                          : result.surface.normalized_nfc;
    reading.lemma =
        citation_lemma(database, lexeme, result.surface.normalized_nfc);
    reading.dictionary =
        dictionary_form(database, lexeme, result.surface.normalized_nfc);
    reading.part = "verb";
    if (lexeme.verb_kind == VerbKind::deponent ||
        lexeme.verb_kind == VerbKind::semideponent) {
        add(reading.part, lexeme.verb_kind == VerbKind::deponent
                              ? (options.compact ? "dep" : "deponent")
                              : (options.compact ? "semidep" : "semideponent"));
    }
    auto morphology = analysis.morphology;
    const bool active_periphrastic =
        analysis.kind == CompoundKind::finite_sum &&
        analysis.source_tense == Tense::future &&
        analysis.source_voice == Voice::active;
    const bool passive_periphrastic =
        analysis.kind == CompoundKind::finite_sum &&
        analysis.source_tense == Tense::future &&
        analysis.source_voice == Voice::passive;
    const bool ordinary_verb = lexeme.verb_kind != VerbKind::deponent &&
                               lexeme.verb_kind != VerbKind::semideponent;
    if (active_periphrastic || passive_periphrastic)
        morphology.voice = Voice::unknown;
    reading.features = features(Morphology{morphology}, lexeme.verb_kind);
    if (ordinary_verb && active_periphrastic)
        add(reading.features, "active periphrastic with " + analysis.auxiliary);
    else if (ordinary_verb && passive_periphrastic)
        add(reading.features,
            "passive periphrastic with " + analysis.auxiliary);
    else
        add(reading.features, "compound with " + analysis.auxiliary);
    reading.meaning = short_meaning(database.meaning(lexeme.meaning));
    reading.note = assessment_note(analysis.assessment, options.detailed);
    if (options.detailed)
        reading.metadata = lexical_metadata(
            lexeme, analysis.source_rule ? &database.rule(*analysis.source_rule)
                                         : nullptr);
    if (options.detailed && active_periphrastic)
        reading.explanation = "A future active participle combines with a form "
                              "of sum to express an action about to happen.";
    else if (options.detailed && passive_periphrastic)
        reading.explanation = "A gerundive combines with a form of sum to "
                              "express necessity or obligation.";
    return reading;
}

[[nodiscard]] Reading roman_reading(const SurfaceForm &surface,
                                    const RomanNumeralIR &analysis,
                                    const HumanOptions options) {
    Reading reading;
    reading.display = surface.normalized_nfc;
    reading.lemma = surface.normalized_nfc;
    reading.dictionary = surface.original_utf8;
    reading.part = "numeral";
    reading.features = "Roman numeral";
    reading.meaning = std::to_string(analysis.value);
    reading.note = assessment_note(analysis.assessment, options.detailed);
    if (!analysis.well_formed) {
        if (!reading.note.empty())
            reading.note.append("; ");
        reading.note.append("unusual Roman numeral spelling");
    }
    return reading;
}

using Candidate = std::variant<const AnalysisIR *, const CompoundAnalysisIR *,
                               const RomanNumeralIR *>;

void append_lexical(Unit &unit, const Database &database,
                    const SurfaceForm &surface,
                    const std::vector<AnalysisIR> &analyses,
                    const ResultFilters &filters, const HumanOptions options) {
    std::vector<std::pair<AnalysisOrderKey, const AnalysisIR *>> ordered;
    for (const auto &analysis : analyses) {
        if (!excludes(analysis.assessment, filters))
            ordered.emplace_back(
                analysis_order_key(database, surface, analysis), &analysis);
    }
    std::ranges::sort(ordered, {}, &decltype(ordered)::value_type::first);
    for (const auto &[key, analysis] : ordered) {
        static_cast<void>(key);
        unit.readings.push_back(
            lexical_reading(database, surface, *analysis, options));
    }
    unit.filtered_all = !analyses.empty() && ordered.empty();
}

void append_main(Unit &unit, const Engine &engine, const QueryResult &result,
                 const ResultFilters &filters, const HumanOptions options) {
    const auto &database = engine.database();
    std::vector<std::pair<AnalysisOrderKey, Candidate>> ordered;
    if (unit.name != "construction")
        for (const auto &analysis : result.analyses) {
            if (!excludes(analysis.assessment, filters))
                ordered.emplace_back(
                    analysis_order_key(database, result.surface, analysis),
                    &analysis);
        }
    for (const auto &analysis : result.compound_analyses) {
        if (!excludes(analysis.assessment, filters))
            ordered.emplace_back(analysis_order_key(database, analysis),
                                 &analysis);
    }
    if (unit.name != "construction")
        for (const auto &artificial : result.artificial_analyses) {
            std::visit(
                [&](const auto &analysis) {
                    if (!excludes(analysis.assessment, filters))
                        ordered.emplace_back(analysis_order_key(analysis),
                                             &analysis);
                },
                artificial);
        }
    std::ranges::sort(ordered, {}, &decltype(ordered)::value_type::first);
    for (const auto &[key, candidate] : ordered) {
        static_cast<void>(key);
        std::visit(
            [&](const auto *analysis) {
                using T =
                    std::remove_cv_t<std::remove_pointer_t<decltype(analysis)>>;
                if constexpr (std::is_same_v<T, AnalysisIR>)
                    unit.readings.push_back(lexical_reading(
                        database, result.surface, *analysis, options));
                else if constexpr (std::is_same_v<T, CompoundAnalysisIR>)
                    unit.readings.push_back(
                        compound_reading(database, result, *analysis, options));
                else
                    unit.readings.push_back(
                        roman_reading(result.surface, *analysis, options));
            },
            candidate);
    }
    unit.filtered_all =
        ordered.empty() &&
        (unit.name == "construction"
             ? !result.compound_analyses.empty()
             : !result.analyses.empty() || !result.compound_analyses.empty() ||
                   !result.artificial_analyses.empty());
}

void append_token(Unit &unit, const Database &database,
                  const IndependentTokenAnalysisIR &token,
                  const ResultFilters &filters, const HumanOptions options) {
    std::vector<std::pair<AnalysisOrderKey, Candidate>> ordered;
    for (const auto &analysis : token.analyses) {
        if (!excludes(analysis.assessment, filters))
            ordered.emplace_back(
                analysis_order_key(database, token.surface, analysis),
                &analysis);
    }
    for (const auto &artificial : token.artificial_analyses) {
        std::visit(
            [&](const auto &analysis) {
                if (!excludes(analysis.assessment, filters))
                    ordered.emplace_back(analysis_order_key(analysis),
                                         &analysis);
            },
            artificial);
    }
    std::ranges::sort(ordered, {}, &decltype(ordered)::value_type::first);
    for (const auto &[key, candidate] : ordered) {
        static_cast<void>(key);
        std::visit(
            [&](const auto *analysis) {
                using T =
                    std::remove_cv_t<std::remove_pointer_t<decltype(analysis)>>;
                if constexpr (std::is_same_v<T, AnalysisIR>)
                    unit.readings.push_back(lexical_reading(
                        database, token.surface, *analysis, options));
                else if constexpr (std::is_same_v<T, RomanNumeralIR>)
                    unit.readings.push_back(
                        roman_reading(token.surface, *analysis, options));
            },
            candidate);
    }
    unit.filtered_all = ordered.empty() && (!token.analyses.empty() ||
                                            !token.artificial_analyses.empty());
}

[[nodiscard]] std::vector<Unit> units(const Engine &engine,
                                      const QueryResult &result,
                                      const ResultFilters &filters,
                                      const HumanOptions options) {
    std::vector<Unit> output;
    Unit main;
    main.name =
        !result.compound_analyses.empty() && !result.independent_tokens.empty()
            ? "construction"
            : "main";
    main.input = result.multi_token_query
                     ? result.multi_token_query->original_utf8
                     : result.surface.original_utf8;
    main.status = status_name(result.status);
    if (!result.diagnostics.empty())
        main.diagnostic = diagnostic_text(result.diagnostics.back().code);
    append_main(main, engine, result, filters, options);
    output.push_back(std::move(main));
    for (std::size_t index{}; index < result.independent_tokens.size();
         ++index) {
        const auto &token = result.independent_tokens[index];
        Unit unit;
        unit.name = "token:" + std::to_string(index + 1U);
        unit.input = token.surface.original_utf8;
        unit.status = status_name(token.status);
        if (!token.diagnostics.empty())
            unit.diagnostic = diagnostic_text(token.diagnostics.back().code);
        append_token(unit, engine.database(), token, filters, options);
        output.push_back(std::move(unit));
    }
    if (result.two_word_suggestion) {
        std::vector<Unit> suggestion;
        std::string split;
        bool all_have_readings = true;
        for (std::size_t index{};
             index < result.two_word_suggestion->segments.size(); ++index) {
            const auto &segment = result.two_word_suggestion->segments[index];
            Unit unit;
            unit.name = "suggestion:" + std::to_string(index + 1U);
            unit.input = segment.surface.original_utf8;
            unit.status = "suggested";
            unit.split_component = segment.surface.normalized_nfc;
            if (const auto enclitic =
                    suggested_enclitic(engine.database(), segment)) {
                unit.split_component =
                    enclitic->base + " + -" + enclitic->suffix;
                unit.enclitic = "enclitic -" + enclitic->suffix;
            }
            if (!split.empty())
                split.append(" + ");
            split.append(unit.split_component);
            append_lexical(unit, engine.database(), segment.surface,
                           segment.analyses, filters, options);
            if (!unit.enclitic.empty())
                for (auto &reading : unit.readings) {
                    if (!reading.note.empty())
                        reading.note.append("; ");
                    reading.note.append(unit.enclitic);
                }
            all_have_readings &= !unit.readings.empty();
            suggestion.push_back(std::move(unit));
        }
        if (all_have_readings) {
            output.front().diagnostic =
                "no confirmed reading; possible split: " + split;
            for (auto &unit : suggestion)
                output.push_back(std::move(unit));
        }
    }
    return output;
}

void field(std::string &output, const std::string_view value) {
    output.push_back('\t');
    output.append(safe_text(value));
}

[[nodiscard]] std::string status_note(const Unit &unit) {
    if (unit.filtered_all)
        return "all readings hidden by filters";
    if (!unit.diagnostic.empty())
        return unit.diagnostic;
    if (unit.status == "unknown")
        return "no reading found";
    if (unit.status == "error")
        return "analysis failed";
    return "no readings";
}

[[nodiscard]] std::string paint(const std::string_view value,
                                const std::string_view code, const bool color) {
    return color ? "\x1b[" + std::string{code} + "m" + std::string{value} +
                       "\x1b[0m"
                 : std::string{value};
}

} // namespace

std::string human_header() {
    return "result\tunit\treading\tstatus\tinput\tdisplay\t"
           "quantity_coverage\tlemma\tpart\tfeatures\tmeaning\tnote\n";
}

std::string render_human(const Engine &engine, const QueryResult &result,
                         const ResultFilters &filters,
                         const HumanOptions options,
                         const std::size_t result_number) {
    if (!engine.owns(result))
        throw std::logic_error{"analysis result belongs to another dataset"};
    if (!engine.supports_full_analysis())
        throw std::logic_error{"human output requires a full WWDB"};
    const auto values = units(engine, result, filters, options);
    std::string output;
    if (options.compact) {
        for (const auto &unit : values) {
            const auto count = std::max<std::size_t>(1U, unit.readings.size());
            for (std::size_t index{}; index < count; ++index) {
                const Reading *reading = index < unit.readings.size()
                                             ? &unit.readings[index]
                                             : nullptr;
                output.append(std::to_string(result_number));
                field(output, unit.name);
                field(output, reading ? std::to_string(index + 1U) : "0");
                field(output, unit.status);
                field(output, unit.input);
                field(output, reading ? reading->display : "");
                field(output, reading ? reading->quantity_coverage : "none");
                field(output, reading ? reading->lemma : "");
                field(output, reading ? reading->part : "");
                field(output, reading ? reading->features : "");
                field(output, reading ? reading->meaning : "");
                auto note = reading ? reading->note : status_note(unit);
                if (reading && !reading->explanation.empty()) {
                    if (!note.empty())
                        note.append("; ");
                    note.append(reading->explanation);
                }
                if (reading && !reading->metadata.empty()) {
                    if (!note.empty())
                        note.append("; ");
                    note.append(reading->metadata);
                }
                field(output, note);
                output.push_back('\n');
            }
        }
        return output;
    }

    for (std::size_t unit_index{}; unit_index < values.size(); ++unit_index) {
        const auto &unit = values[unit_index];
        if (unit_index != 0U)
            output.push_back('\n');
        const auto heading =
            unit.name == "main" ? safe_text(unit.input)
            : unit.name == "construction"
                ? "Construction: " + safe_text(unit.input)
            : unit.name.starts_with("token:")
                ? "Token " + unit.name.substr(6U) + ": " + safe_text(unit.input)
                : "Possible split, part " + unit.name.substr(11U) + ": " +
                      safe_text(unit.split_component);
        output.append(paint(heading, "1;36", options.color));
        output.push_back('\n');
        if (unit.readings.empty()) {
            output.append("  ");
            output.append(status_note(unit));
            output.push_back('\n');
            continue;
        }
        for (std::size_t index{}; index < unit.readings.size(); ++index) {
            const auto &reading = unit.readings[index];
            output.append(std::to_string(index + 1U));
            output.append(". ");
            output.append(safe_text(reading.dictionary));
            output.push_back('\n');
            output.append("   ");
            output.append(safe_text(reading.part));
            output.push_back('\n');
            if (!reading.features.empty()) {
                output.append("   ");
                output.append(safe_text(reading.features));
                output.push_back('\n');
            }
            if (!reading.meaning.empty()) {
                output.append("   ");
                output.append(safe_text(reading.meaning));
                output.push_back('\n');
            }
            output.append("   Form: input ");
            output.append(safe_text(unit.input));
            output.append(" · display ");
            output.append(safe_text(reading.display));
            output.append(" · quantity evidence ");
            output.append(reading.quantity_coverage);
            output.push_back('\n');
            if (options.detailed && !reading.annotated.empty()) {
                output.append("   Database-marked form: ");
                output.append(safe_text(reading.annotated));
                output.push_back('\n');
            }
            if (!reading.note.empty()) {
                output.append("   ");
                output.append(paint("!", "1;33", options.color));
                output.push_back(' ');
                output.append(safe_text(reading.note));
                output.push_back('\n');
            }
            if (!reading.explanation.empty()) {
                output.append("   Explanation: ");
                output.append(safe_text(reading.explanation));
                output.push_back('\n');
            }
            if (!reading.metadata.empty()) {
                output.append("   Lexical details: ");
                output.append(safe_text(reading.metadata));
                output.push_back('\n');
            }
            if (index + 1U < unit.readings.size())
                output.push_back('\n');
        }
    }
    return output;
}

} // namespace words::client
