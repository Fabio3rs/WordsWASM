#include "words/json.hpp"
#include "words/artificial.hpp"
#include "words/lexeme.hpp"
#include "words/projection.hpp"
#include "words/semantics.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <ranges>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

namespace words {
namespace {

using Json = nlohmann::ordered_json;

constexpr std::string_view general_dictionary_name{"general"};
constexpr std::string_view unique_dictionary_name{"unique"};
constexpr std::string_view roman_dictionary_name{"roman-numeral"};
constexpr std::string_view syncope_method_name{"syncope"};
constexpr std::string_view orthographic_method_name{"orthographic"};
constexpr std::string_view regular_method_name{"regular"};
constexpr std::string_view derived_method_name{"derived"};
constexpr std::string_view compound_method_name{"compound"};

[[nodiscard]] constexpr std::string_view
dictionary_name(const DictionaryKind dictionary) noexcept {
    return dictionary == DictionaryKind::unique ? unique_dictionary_name
                                                : general_dictionary_name;
}

[[nodiscard]] constexpr std::string_view
derivation_method(const std::optional<std::string_view> rewrite_method,
                  const DictionaryKind dictionary,
                  const bool has_addons) noexcept {
    if (rewrite_method) {
        return *rewrite_method;
    }
    if (dictionary == DictionaryKind::unique) {
        return unique_dictionary_name;
    }
    return has_addons ? derived_method_name : regular_method_name;
}

[[nodiscard]] Json nullable_semantic(const std::string_view value) {
    return value.empty() ? Json(nullptr) : Json(value);
}

[[nodiscard]] Json age_json(const Age value) {
    return nullable_semantic(age_name(value));
}

[[nodiscard]] Json lexical_frequency_json(const LexicalFrequency value) {
    return nullable_semantic(lexical_frequency_name(value));
}

[[nodiscard]] Json rule_frequency_json(const RuleFrequency value) {
    return nullable_semantic(rule_frequency_name(value));
}

[[nodiscard]] Json subject_json(const SubjectArea value) {
    return nullable_semantic(subject_name(value));
}

[[nodiscard]] Json geography_json(const Geography value) {
    return nullable_semantic(geography_name(value));
}

[[nodiscard]] Json source_json(const Source value) {
    return nullable_semantic(source_name(value));
}

[[nodiscard]] Json noun_kind_json(const NounKind value) {
    return nullable_semantic(noun_kind_name(value));
}

[[nodiscard]] Json pronoun_kind_json(const PronounKind value) {
    return nullable_semantic(pronoun_kind_name(value));
}

[[nodiscard]] Json gender_json(const Gender value) {
    return nullable_semantic(gender_name(value));
}

[[nodiscard]] Json case_json(const GrammaticalCase value) {
    return nullable_semantic(case_name(value));
}

[[nodiscard]] Json number_json(const GrammaticalNumber value) {
    return nullable_semantic(number_name(value));
}

[[nodiscard]] Json degree_json(const Degree value) {
    return nullable_semantic(degree_name(value));
}

[[nodiscard]] Json numeral_type_json(const NumeralType value) {
    return nullable_semantic(numeral_type_name(value));
}

[[nodiscard]] Json tense_json(const Tense value) {
    return nullable_semantic(tense_name(value));
}

[[nodiscard]] Json voice_json(const Voice value) {
    return nullable_semantic(voice_name(value));
}

[[nodiscard]] Json mood_json(const Mood value) {
    return nullable_semantic(mood_name(value));
}

[[nodiscard]] Json verb_kind_json(const VerbKind value) {
    return nullable_semantic(verb_kind_name(value));
}

[[nodiscard]] Json paradigm_json(const std::uint8_t value) {
    if (value == 0U) {
        return nullptr;
    }
    return static_cast<std::uint32_t>(value);
}

[[nodiscard]] Json diagnostic_json(const Diagnostic &diagnostic) {
    Json parameters = Json::object();
    if (diagnostic.part_of_speech) {
        parameters["partOfSpeech"] =
            lexical_part_name(*diagnostic.part_of_speech);
    }
    return Json{
        {"code", diagnostic_code_name(diagnostic.code)},
        {"severity", diagnostic_severity_name(diagnostic.severity)},
        {"parameters", std::move(parameters)},
    };
}

[[nodiscard]] Json query_json(const QueryResult &result) {
    const auto text =
        result.multi_token_query
            ? std::string_view{result.multi_token_query->original_utf8}
            : std::string_view{result.surface.original_utf8};
    const auto normalized =
        result.multi_token_query
            ? std::string_view{result.multi_token_query->normalized_nfc}
            : std::string_view{result.surface.normalized_nfc};
    return Json{
        {"text", text},
        {"normalized", normalized},
        {"mode", "latin"},
    };
}

[[nodiscard]] Json morphology_json(const NounMorphology &morphology) {
    return Json{
        {"declension", paradigm_json(morphology.declension)},
        {"variant", paradigm_json(morphology.variant)},
        {"case", case_json(morphology.grammatical_case)},
        {"number", number_json(morphology.number)},
        {"gender", gender_json(morphology.gender)},
    };
}

[[nodiscard]] Json morphology_json(const PronounMorphology &morphology) {
    return Json{
        {"declension", paradigm_json(morphology.declension)},
        {"variant", paradigm_json(morphology.variant)},
        {"case", case_json(morphology.grammatical_case)},
        {"number", number_json(morphology.number)},
        {"gender", gender_json(morphology.gender)},
    };
}

[[nodiscard]] Json morphology_json(const AdjectiveMorphology &morphology) {
    return Json{
        {"declension", paradigm_json(morphology.declension)},
        {"variant", paradigm_json(morphology.variant)},
        {"case", case_json(morphology.grammatical_case)},
        {"number", number_json(morphology.number)},
        {"gender", gender_json(morphology.gender)},
        {"degree", degree_json(morphology.degree)},
    };
}

[[nodiscard]] Json morphology_json(const NumeralMorphology &morphology) {
    return Json{
        {"declension", paradigm_json(morphology.declension)},
        {"variant", paradigm_json(morphology.variant)},
        {"case", case_json(morphology.grammatical_case)},
        {"number", number_json(morphology.number)},
        {"gender", gender_json(morphology.gender)},
        {"numeralType", numeral_type_json(morphology.numeral_type)},
    };
}

[[nodiscard]] Json morphology_json(const AdverbMorphology &morphology) {
    return Json{{"degree", degree_json(morphology.degree)}};
}

[[nodiscard]] Json morphology_json(const VerbMorphology &morphology) {
    return Json{
        {"conjugation", paradigm_json(morphology.conjugation)},
        {"variant", paradigm_json(morphology.variant)},
        {"tense", tense_json(morphology.tense)},
        {"voice", voice_json(morphology.voice)},
        {"mood", mood_json(morphology.mood)},
        {"person", paradigm_json(std::to_underlying(morphology.person))},
        {"number", number_json(morphology.number)},
    };
}

[[nodiscard]] Json morphology_json(const ParticipleMorphology &morphology) {
    return Json{
        {"conjugation", paradigm_json(morphology.conjugation)},
        {"variant", paradigm_json(morphology.variant)},
        {"case", case_json(morphology.grammatical_case)},
        {"number", number_json(morphology.number)},
        {"gender", gender_json(morphology.gender)},
        {"tense", tense_json(morphology.tense)},
        {"voice", voice_json(morphology.voice)},
    };
}

[[nodiscard]] Json morphology_json(const SupineMorphology &morphology) {
    return Json{
        {"conjugation", paradigm_json(morphology.conjugation)},
        {"variant", paradigm_json(morphology.variant)},
        {"case", case_json(morphology.grammatical_case)},
        {"number", number_json(morphology.number)},
        {"gender", gender_json(morphology.gender)},
    };
}

[[nodiscard]] Json morphology_json(const PrepositionMorphology &morphology) {
    return Json{{"governs", case_json(morphology.governs)}};
}

[[nodiscard]] Json morphology_json(const InvariableMorphology & /*unused*/) {
    return Json::object();
}

[[nodiscard]] Json lexical_properties_json(const LexemeRecord &lexeme) {
    const auto declension = [&lexeme] {
        return std::get<DeclensionParadigm>(lexeme_paradigm(lexeme));
    };
    const auto conjugation = [&lexeme] {
        return std::get<ConjugationParadigm>(lexeme_paradigm(lexeme));
    };
    switch (std::to_underlying(lexeme.part_of_speech)) {
    case std::to_underlying(PartOfSpeech::noun):
        return Json{{"declension", paradigm_json(declension().number)},
                    {"variant", paradigm_json(declension().variant)},
                    {"gender", gender_json(lexeme.gender)},
                    {"nounKind", noun_kind_json(lexeme.noun_kind)}};
    case std::to_underlying(PartOfSpeech::pronoun):
    case std::to_underlying(PartOfSpeech::pack):
        return Json{{"declension", paradigm_json(declension().number)},
                    {"variant", paradigm_json(declension().variant)},
                    {"pronounKind", pronoun_kind_json(lexeme.pronoun_kind)}};
    case std::to_underlying(PartOfSpeech::adjective):
        return Json{{"declension", paradigm_json(declension().number)},
                    {"variant", paradigm_json(declension().variant)},
                    {"degree", degree_json(lexeme.adjective_degree)}};
    case std::to_underlying(PartOfSpeech::numeral):
        return Json{{"declension", paradigm_json(declension().number)},
                    {"variant", paradigm_json(declension().variant)},
                    {"numeralType", numeral_type_json(lexeme.numeral_type)},
                    {"numeralValue", lexeme.numeral_value}};
    case std::to_underlying(PartOfSpeech::adverb):
        return Json{{"degree", degree_json(lexeme.adverb_degree)}};
    case std::to_underlying(PartOfSpeech::verb):
        return Json{{"conjugation", paradigm_json(conjugation().number)},
                    {"variant", paradigm_json(conjugation().variant)},
                    {"verbKind", verb_kind_json(lexeme.verb_kind)}};
    case std::to_underlying(PartOfSpeech::preposition):
        return Json{{"governs", case_json(lexeme.governs)}};
    default:
        return Json::object();
    }
}

[[nodiscard]] Json
derivation_json(const Database &database, const DerivationIR &derivation,
                const DictionaryKind dictionary,
                const std::optional<RewrittenFormIR> &rewritten_form) {
    Json steps = Json::array();
    std::optional<std::string_view> rewrite_method;
    const auto append_addon = [&](const AddonId addon_id) {
        Json step;
        const auto kind = database.addon_kind(addon_id);
        if (kind == AddonKind::prefix || kind == AddonKind::tickon) {
            const auto &prefix = database.prefix(addon_id);
            step = Json{
                {"type", "prefix"},
                {"text", database.prefix_string(prefix.fix)},
                {"meaning", database.prefix_meaning(prefix.meaning)},
            };
        } else if (kind == AddonKind::suffix) {
            const auto &suffix = database.suffix(addon_id);
            step = Json{
                {"type", "suffix"},
                {"text", database.suffix_string(suffix.fix)},
                {"meaning", database.suffix_meaning(suffix.meaning)},
            };
        } else {
            const auto &tackon = database.tackon(addon_id);
            step = Json{
                {"type", kind == AddonKind::packon ? "packon" : "tackon"},
                {"text", database.tackon_string(tackon.fix)},
                {"meaning", database.tackon_meaning(tackon.meaning)},
            };
        }
        steps.push_back(std::move(step));
    };

    const auto addon_steps = derivation.steps();
    const auto leading_addons =
        rewritten_form
            ? std::min<std::size_t>(rewritten_form->leading_addon_count,
                                    addon_steps.size())
            : 0U;
    for (const auto addon_id : addon_steps.first(leading_addons)) {
        append_addon(addon_id);
    }
    if (rewritten_form) {
        for (const auto rewrite_id : rewritten_form->steps()) {
            const auto &rewrite = database.rewrite(rewrite_id);
            const auto method = rewrite.kind == RewriteKind::syncope
                                    ? syncope_method_name
                                    : orthographic_method_name;
            if (!rewrite_method) {
                rewrite_method = method;
            }
            Json step{
                {"type", method},
                {"meaning", database.rewrite_meaning(rewrite.meaning)},
                {"rule", database.rewrite_string(rewrite.name)},
            };
            const auto before = database.rewrite_string(rewrite.before);
            const auto after = database.rewrite_string(rewrite.after);
            if (!before.empty()) {
                step["before"] = before;
            }
            if (!after.empty()) {
                step["after"] = after;
            }
            steps.push_back(std::move(step));
        }
    }
    for (const auto addon_id : addon_steps.subspan(leading_addons)) {
        append_addon(addon_id);
    }
    return Json{
        // Unique remains the method even when a recursively stripped tackon
        // contributes steps; this is the precedence used by the Ada emitter.
        {"method",
         derivation_method(rewrite_method, dictionary, derivation.count != 0U)},
        {"steps", std::move(steps)},
    };
}

[[nodiscard]] std::string_view
compound_meaning(const CompoundAnalysisIR &analysis) noexcept {
    if (analysis.kind == CompoundKind::iri) {
        return "SUPINE + iri => FUT PASSIVE INF - to be about/going/ready to "
               "be ~";
    }
    if (analysis.kind == CompoundKind::finite_sum) {
        if (analysis.source_tense == Tense::perfect) {
            return "PERF PASSIVE PPL + verb TO_BE => PASSIVE perfect system";
        }
        return analysis.source_voice == Voice::active
                   ? "FUT ACTIVE PPL + verb TO_BE => ACTIVE Periphrastic - "
                     "about to, going to"
                   : "FUT PASSIVE PPL + verb TO_BE => PASSIVE Periphrastic "
                     "- should/ought/had to";
    }
    if (analysis.kind == CompoundKind::esse) {
        if (analysis.source_tense == Tense::perfect) {
            return "PERF PASSIVE PPL + esse => PERF PASSIVE INF";
        }
        return analysis.source_voice == Voice::active
                   ? "FUT ACTIVE PPL + esse => PRES Periphrastic/FUT ACTIVE "
                     "INF - be about/going to"
                   : "FUT PASSIVE PPL + esse => PRES PASSIVE INF";
    }
    return analysis.source_voice == Voice::active
               ? "FUT ACT PPL + fuisse => PERF ACT INF Periphrastic - to have "
                 "been about/going to"
               : "FUT PASSIVE PPL + fuisse => PERF PASSIVE INF Periphrastic "
                 "- about to, going to";
}

[[nodiscard]] Json full_lexeme_json(const Database &database,
                                    const LexemeRecord &lexeme,
                                    const std::string_view fallback) {
    Json properties = lexeme.dictionary == DictionaryKind::unique
                          ? Json::object()
                          : lexical_properties_json(lexeme);
    Json metadata{
        {"age", age_json(lexeme.age)},
        {"subject", subject_json(lexeme.subject)},
        {"geography", geography_json(lexeme.geography)},
        {"frequency", lexical_frequency_json(lexeme.frequency)},
        {"source", source_json(lexeme.source)},
    };
    return Json{
        {"dictionary", dictionary_name(lexeme.dictionary)},
        {"entryId", lexeme.dictionary_entry + 1U},
        {"dictionaryForm", dictionary_form(database, lexeme, fallback)},
        {"partOfSpeech", lexical_part_name(lexeme.part_of_speech)},
        {"meaning", normalized_meaning(database.meaning(lexeme.meaning))},
        {"properties", std::move(properties)},
        {"metadata", std::move(metadata)},
    };
}

[[nodiscard]] Json full_analysis(const Engine &engine,
                                 const SurfaceForm &surface,
                                 const AnalysisIR &analysis) {
    const auto &database = engine.database();
    const auto &lexeme = database.lexeme(analysis.lexeme);
    const auto stem =
        analysis.derivation.rewritten_form
            ? std::string_view{analysis.derivation.rewritten_form->stem}
            : surface.slice(analysis.stem);
    const auto ending =
        analysis.derivation.rewritten_form
            ? std::string_view{analysis.derivation.rewritten_form->ending}
            : surface.slice(analysis.ending);

    const std::string_view analysis_part_of_speech =
        morphology_part_name(analysis.morphology, lexeme.part_of_speech);
    Json form{
        {"stem", stem},
        {"stemKey", paradigm_json(analysis.stem_key)},
        {"ending", ending},
        {"rule", Json{{"age", nullptr}, {"frequency", nullptr}}},
    };
    if (analysis.rule) {
        const auto &rule = database.rule(*analysis.rule);
        form["rule"] = Json{
            {"age", age_json(rule.age)},
            {"frequency", rule_frequency_json(rule.frequency)},
        };
    }
    const auto morphology =
        std::visit([](const auto &value) { return morphology_json(value); },
                   analysis.morphology);
    return Json{
        {"partOfSpeech", analysis_part_of_speech},
        {"lexeme", full_lexeme_json(database, lexeme, surface.normalized_nfc)},
        {"form", std::move(form)},
        {"morphology", morphology},
        {"derivation",
         derivation_json(database, analysis.derivation, lexeme.dictionary,
                         analysis.derivation.rewritten_form)},
    };
}

[[nodiscard]] Json full_compound_analysis(const Engine &engine,
                                          const QueryResult &result,
                                          const CompoundAnalysisIR &analysis) {
    const auto &database = engine.database();
    const auto &lexeme = database.lexeme(analysis.lexeme);
    std::string stem =
        analysis.kind == CompoundKind::iri ? "SUPINE + " : "PPL+";
    stem.append(analysis.auxiliary);

    auto derivation =
        derivation_json(database, analysis.source_derivation, lexeme.dictionary,
                        analysis.source_derivation.rewritten_form);
    derivation["method"] = compound_method_name;
    derivation["steps"].push_back(Json{
        {"type", compound_method_name},
        {"text", analysis.auxiliary},
        {"meaning", compound_meaning(analysis)},
        {"rule", compound_kind_name(analysis.kind)},
    });

    return Json{
        {"partOfSpeech", lexical_part_name(PartOfSpeech::verb)},
        {"lexeme",
         full_lexeme_json(database, lexeme, result.surface.normalized_nfc)},
        {"form",
         Json{{"stem", std::move(stem)},
              {"stemKey", nullptr},
              {"ending", ""},
              {"rule",
               Json{{"age", nullptr},
                    {"frequency",
                     rule_frequency_name(RuleFrequency::most_frequent)}}}}},
        {"morphology", morphology_json(analysis.morphology)},
        {"derivation", std::move(derivation)},
    };
}

[[nodiscard]] std::string roman_meaning(const RomanNumeralIR &analysis) {
    std::string meaning = std::to_string(analysis.value);
    meaning.append(analysis.well_formed ? "  as a ROMAN NUMERAL;"
                                        : "  as ill-formed ROMAN NUMERAL?;");
    return meaning;
}

[[nodiscard]] Json full_roman_analysis(const Database &database,
                                       const QueryResult &result,
                                       const RomanNumeralIR &analysis) {
    const auto meaning = roman_meaning(analysis);
    const auto stem = std::string_view{result.surface.original_utf8}.substr(
        analysis.stem.begin, analysis.stem.count);
    Json steps = Json::array();
    for (const auto addon_id : analysis.derivation.steps()) {
        const auto &tackon = database.tackon(addon_id);
        steps.push_back(Json{
            {"type", database.addon_kind(addon_id) == AddonKind::packon
                         ? "packon"
                         : "tackon"},
            {"text", database.tackon_string(tackon.fix)},
            {"meaning", database.tackon_meaning(tackon.meaning)},
        });
    }
    steps.push_back(Json{
        {"type", roman_dictionary_name}, {"text", ""}, {"meaning", meaning}});
    return Json{
        {"partOfSpeech", lexical_part_name(PartOfSpeech::numeral)},
        {"lexeme",
         Json{{"dictionary", roman_dictionary_name},
              {"entryId", nullptr},
              {"dictionaryForm", result.surface.original_utf8},
              {"partOfSpeech", lexical_part_name(PartOfSpeech::numeral)},
              {"meaning", meaning},
              {"properties", Json::object()},
              {"metadata", Json{{"age", nullptr},
                                {"subject", nullptr},
                                {"geography", nullptr},
                                {"frequency", nullptr},
                                {"source", nullptr}}}}},
        {"form",
         Json{{"stem", stem},
              {"stemKey", nullptr},
              {"ending", ""},
              {"rule",
               Json{{"age", nullptr},
                    {"frequency",
                     rule_frequency_name(analysis.well_formed
                                             ? RuleFrequency::most_frequent
                                             : RuleFrequency::infrequent)}}}}},
        {"morphology",
         Json{{"declension", roman_numeral_morphology().declension},
              {"variant", nullptr},
              {"case", nullptr},
              {"number", nullptr},
              {"gender", nullptr},
              {"numeralType", numeral_type_name(NumeralType::cardinal)}}},
        {"derivation",
         Json{{"method", roman_dictionary_name}, {"steps", std::move(steps)}}},
    };
}

struct SearchHit final {
    std::uint32_t lexeme{};
    std::optional<std::uint32_t> rule;
    std::array<std::uint32_t, 3> addons{};
    std::uint8_t addon_count{};
    std::array<std::uint32_t, 2> rewrites{};
    std::uint8_t rewrite_count{};
    CompoundKind compound_kind{CompoundKind::unknown};
    std::string compound_auxiliary;
    auto operator<=>(const SearchHit &) const = default;
};

void append_search_hit(std::vector<SearchHit> &output, const LexemeId lexeme,
                       const std::optional<RuleId> rule,
                       const DerivationIR &derivation,
                       const CompoundKind compound_kind = CompoundKind::unknown,
                       const std::string_view auxiliary = {}) {
    SearchHit hit;
    hit.lexeme = lexeme.value();
    if (rule) {
        hit.rule = rule->value();
    }
    hit.addon_count = derivation.count;
    if (derivation.rewritten_form) {
        hit.rewrite_count = derivation.rewritten_form->count;
        std::ranges::transform(derivation.rewritten_form->rules,
                               hit.rewrites.begin(),
                               [](const RewriteId id) { return id.value(); });
    }
    std::ranges::transform(derivation.addon_ids, hit.addons.begin(),
                           [](const AddonId id) { return id.value(); });
    hit.compound_kind = compound_kind;
    hit.compound_auxiliary = auxiliary;
    output.push_back(std::move(hit));
}

[[nodiscard]] Json search_hits_json(std::vector<SearchHit> ordered_hits) {
    std::ranges::sort(ordered_hits);
    const auto unique = std::ranges::unique(ordered_hits).begin();
    ordered_hits.erase(unique, ordered_hits.end());

    Json hits = Json::array();
    for (const auto &hit : ordered_hits) {
        Json addon_ids = Json::array();
        for (std::size_t index = 0; index < hit.addon_count; ++index) {
            addon_ids.push_back(hit.addons.at(index));
        }
        Json serialized_hit{
            {"lexemeId", hit.lexeme},
            {"ruleId", hit.rule ? Json(*hit.rule) : Json(nullptr)},
            {"addonIds", std::move(addon_ids)},
            {"scoreFlags", 0},
        };
        if (hit.rewrite_count > 0U) {
            Json rewrite_ids = Json::array();
            for (std::size_t index = 0; index < hit.rewrite_count; ++index) {
                rewrite_ids.push_back(hit.rewrites.at(index));
            }
            serialized_hit["rewriteIds"] = std::move(rewrite_ids);
        }
        if (hit.compound_kind != CompoundKind::unknown) {
            serialized_hit["compound"] = Json{
                {"construction", compound_kind_name(hit.compound_kind)},
                {"auxiliary", hit.compound_auxiliary},
            };
        }
        hits.push_back(std::move(serialized_hit));
    }
    return hits;
}

[[nodiscard]] Json
full_two_word_suggestion(const Engine &engine,
                         const TwoWordSuggestionIR &suggestion) {
    Json segments = Json::array();
    for (const auto &segment : suggestion.segments) {
        std::vector<std::pair<AnalysisOrderKey, Json>> ordered;
        ordered.reserve(segment.analyses.size());
        for (const auto &analysis : segment.analyses) {
            auto value = full_analysis(engine, segment.surface, analysis);
            ordered.emplace_back(analysis_order_key(engine.database(),
                                                    segment.surface, analysis),
                                 std::move(value));
        }
        std::ranges::sort(ordered, {},
                          &std::pair<AnalysisOrderKey, Json>::first);
        Json analyses = Json::array();
        for (auto &[key, value] : ordered) {
            static_cast<void>(key);
            analyses.push_back(std::move(value));
        }
        segments.push_back(Json{
            {"text", segment.surface.normalized_nfc},
            {"analyses", std::move(analyses)},
        });
    }
    return Json{
        {"method", "two-words"},
        {"splitAt", suggestion.logical_split},
        {"classification",
         suggestion.both_contain_numeral ? "number-pair" : "unconstrained"},
        {"segments", std::move(segments)},
    };
}

[[nodiscard]] Json
search_two_word_suggestion(const TwoWordSuggestionIR &suggestion) {
    Json segments = Json::array();
    for (const auto &segment : suggestion.segments) {
        std::vector<SearchHit> hits;
        hits.reserve(segment.analyses.size());
        for (const auto &analysis : segment.analyses) {
            append_search_hit(hits, analysis.lexeme, analysis.rule,
                              analysis.derivation);
        }
        segments.push_back(Json{
            {"text", segment.surface.normalized_nfc},
            {"hits", search_hits_json(std::move(hits))},
        });
    }
    return Json{
        {"method", "two-words"},
        {"splitAt", suggestion.logical_split},
        {"classification",
         suggestion.both_contain_numeral ? "number-pair" : "unconstrained"},
        {"segments", std::move(segments)},
    };
}

} // namespace

std::string analysis_json(const Engine &engine, const QueryResult &result) {
    if (!engine.owns(result)) {
        throw std::logic_error{
            "analysis result belongs to a different dataset"};
    }
    if (!engine.supports_full_analysis()) {
        throw std::logic_error{
            "analysis JSON requires a full WWDB with meanings"};
    }
    Json analyses = Json::array();
    if (result.status == QueryStatus::analyzed) {
        std::vector<std::pair<AnalysisOrderKey, Json>> ordered;
        ordered.reserve(result.analyses.size() +
                        result.compound_analyses.size() +
                        result.artificial_analyses.size());
        for_each_analysis(result, [&](const auto &analysis) {
            using Analysis = std::remove_cvref_t<decltype(analysis)>;
            if constexpr (std::is_same_v<Analysis, AnalysisIR>) {
                auto value = full_analysis(engine, result.surface, analysis);
                ordered.emplace_back(analysis_order_key(engine.database(),
                                                        result.surface,
                                                        analysis),
                                     std::move(value));
            } else if constexpr (std::is_same_v<Analysis, CompoundAnalysisIR>) {
                auto value = full_compound_analysis(engine, result, analysis);
                ordered.emplace_back(
                    analysis_order_key(engine.database(), analysis),
                    std::move(value));
            } else if constexpr (std::is_same_v<Analysis, RomanNumeralIR>) {
                auto value =
                    full_roman_analysis(engine.database(), result, analysis);
                ordered.emplace_back(analysis_order_key(analysis),
                                     std::move(value));
            } else {
                static_assert(sizeof(Analysis) == 0U,
                              "new analysis requires JSON projection");
            }
        });
        std::ranges::sort(ordered, {},
                          &std::pair<AnalysisOrderKey, Json>::first);
        for (auto &[key, value] : ordered) {
            static_cast<void>(key);
            analyses.push_back(std::move(value));
        }
    }

    Json diagnostics = Json::array();
    for (const auto &diagnostic : result.diagnostics) {
        diagnostics.push_back(diagnostic_json(diagnostic));
    }
    Json output{
        {"schema", "whitakers-words.analysis"},
        {"schemaVersion", 1},
        {"query", query_json(result)},
        {"status", status_name(result.status)},
        {"analyses", std::move(analyses)},
        {"diagnostics", std::move(diagnostics)},
    };
    if (result.two_word_suggestion) {
        output["suggestions"] = Json::array(
            {full_two_word_suggestion(engine, *result.two_word_suggestion)});
    }
    return output.dump();
}

std::string search_json(const Engine &engine, const QueryResult &result) {
    if (!engine.owns(result)) {
        throw std::logic_error{
            "analysis result belongs to a different dataset"};
    }
    std::vector<SearchHit> ordered_hits;
    if (result.status == QueryStatus::analyzed) {
        ordered_hits.reserve(result.analyses.size() +
                             result.compound_analyses.size());
        for (const auto &analysis : result.analyses) {
            append_search_hit(ordered_hits, analysis.lexeme, analysis.rule,
                              analysis.derivation);
        }
        for (const auto &analysis : result.compound_analyses) {
            append_search_hit(ordered_hits, analysis.lexeme,
                              analysis.source_rule, analysis.source_derivation,
                              analysis.kind, analysis.auxiliary);
        }
    }

    Json hits = search_hits_json(std::move(ordered_hits));
    for (const auto &artificial : result.artificial_analyses) {
        std::visit(
            [&](const auto &analysis) {
                Json addon_ids = Json::array();
                for (const auto id : analysis.derivation.steps()) {
                    addon_ids.push_back(id.value());
                }
                hits.push_back(Json{
                    {"lexemeId", nullptr},
                    {"ruleId", nullptr},
                    {"addonIds", std::move(addon_ids)},
                    {"scoreFlags", 0},
                    {"artificial", Json{{"method", roman_dictionary_name},
                                        {"value", analysis.value},
                                        {"wellFormed", analysis.well_formed}}},
                });
            },
            artificial);
    }
    Json diagnostics = Json::array();
    for (const auto &diagnostic : result.diagnostics) {
        diagnostics.push_back(diagnostic_json(diagnostic));
    }
    Json output{
        {"schema", "whitakers-words.search"},    {"schemaVersion", 1},
        {"datasetId", engine.dataset_id()},      {"query", query_json(result)},
        {"status", status_name(result.status)},  {"hits", std::move(hits)},
        {"diagnostics", std::move(diagnostics)},
    };
    if (result.two_word_suggestion) {
        output["suggestions"] = Json::array(
            {search_two_word_suggestion(*result.two_word_suggestion)});
    }
    return output.dump();
}

} // namespace words
