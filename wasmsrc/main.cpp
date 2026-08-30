#include "words/engine.hpp"
#include "words/lexeme.hpp"
#include "words/semantics.hpp"

#include <emscripten/bind.h>
#include <emscripten/val.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <limits>
#include <memory>
#include <new>
#include <ranges>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace {

struct LoadResult final {
    bool ok{};
    std::string code;
    std::string message;
    std::uint32_t database_bytes{};
    std::string database_kind;
};

constexpr std::string_view full_database_kind{"full"};
constexpr std::string_view search_database_kind{"search"};

struct BrowserQuery final {
    std::string text;
    std::string normalized;
    std::string mode{"latin"};
};

struct BrowserDiagnostic final {
    std::string code;
    std::string severity;
    std::string part_of_speech;
};

// WHY: Embind has no useful JavaScript representation for the morphology
// variant.  A tagged, fixed-shape value object retains every grammatical flag
// without JSON serialization or heap-allocated polymorphic objects.
struct BrowserMorphology final {
    std::string kind;
    std::uint32_t declension{};
    std::uint32_t conjugation{};
    std::uint32_t variant{};
    std::string grammatical_case;
    std::string number;
    std::string gender;
    std::string degree;
    std::string numeral_type;
    std::string tense;
    std::string voice;
    std::string mood;
    std::uint32_t person{};
    std::string governs;
    auto operator<=>(const BrowserMorphology &) const = default;
};

struct BrowserLexicalFlags final {
    std::string dictionary;
    std::uint32_t entry_id{};
    std::string part_of_speech;
    std::uint32_t declension{};
    std::uint32_t conjugation{};
    std::uint32_t variant{};
    std::string gender;
    std::string noun_kind;
    std::string pronoun_kind;
    std::string degree;
    std::string numeral_type;
    std::uint32_t numeral_value{};
    std::string verb_kind;
    std::string governs;
    std::string age;
    std::string subject;
    std::string geography;
    std::string frequency;
    std::string source;
    auto operator<=>(const BrowserLexicalFlags &) const = default;
};

struct BrowserRuleFlags final {
    bool present{};
    std::string age;
    std::string frequency;
    auto operator<=>(const BrowserRuleFlags &) const = default;
};

struct BrowserForm final {
    std::string stem;
    bool has_stem_key{};
    std::uint32_t stem_key{};
    std::string ending;
    std::string recognized;
    auto operator<=>(const BrowserForm &) const = default;
};

struct BrowserDerivationStep final {
    std::string kind;
    std::string target;
    std::uint32_t id{};
    std::string type;
    std::string text;
    std::string rule;
    std::string before;
    std::string after;
    bool has_meaning{};
    std::string meaning;
    auto operator<=>(const BrowserDerivationStep &) const = default;
};

struct BrowserDerivation final {
    std::string method;
    std::vector<BrowserDerivationStep> steps;
    auto operator<=>(const BrowserDerivation &) const = default;
};

struct BrowserSearchHit final {
    bool has_lexeme{true};
    std::string kind{"lexical"};
    std::uint32_t lexeme_id{};
    bool has_rule{};
    std::uint32_t rule_id{};
    std::string lemma;
    bool has_meaning{};
    std::string meaning;
    std::string part_of_speech;
    BrowserForm form;
    std::string quantity_match;
    BrowserMorphology morphology;
    BrowserLexicalFlags lexical;
    BrowserRuleFlags rule;
    BrowserDerivation derivation;
    bool compound{};
    std::string compound_construction;
    std::string compound_auxiliary;
    std::string compound_source_tense;
    std::string compound_source_voice;
    bool artificial{};
    std::string artificial_method;
    std::uint32_t artificial_value{};
    bool artificial_well_formed{};
    auto operator<=>(const BrowserSearchHit &) const = default;
};

struct BrowserSearchSegment final {
    std::string text;
    std::vector<BrowserSearchHit> hits;
};

struct BrowserSearchSuggestion final {
    std::string method{"two-words"};
    std::uint32_t split_at{};
    std::string classification;
    std::vector<BrowserSearchSegment> segments;
};

struct BrowserSearchResult final {
    std::string schema{"whitakers-words.browser-search"};
    std::uint32_t schema_version{3U};
    std::string dataset_id;
    BrowserQuery query;
    std::string status;
    std::vector<BrowserSearchHit> hits;
    std::vector<BrowserDiagnostic> diagnostics;
    std::vector<BrowserSearchSuggestion> suggestions;
};

using words::addon_kind_name;
using words::age_name;
using words::case_name;
using words::compound_kind_name;
using words::degree_name;
using words::gender_name;
using words::geography_name;
using words::lexical_frequency_name;
using words::lexical_part_name;
using words::mood_name;
using words::normalized_meaning;
using words::noun_kind_name;
using words::number_name;
using words::numeral_type_name;
using words::pronoun_kind_name;
using words::quantity_match_name;
using words::rewrite_kind_name;
using words::rule_frequency_name;
using words::source_name;
using words::status_name;
using words::subject_name;
using words::tense_name;
using words::verb_kind_name;
using words::voice_name;

[[nodiscard]] BrowserMorphology
browser_morphology(const words::Morphology &morphology,
                   const words::PartOfSpeech lexical_part) {
    BrowserMorphology output;
    std::visit(
        [&](const auto &value) {
            using Value = std::remove_cvref_t<decltype(value)>;
            if constexpr (std::is_same_v<Value, words::NounMorphology> ||
                          std::is_same_v<Value, words::PronounMorphology>) {
                output.kind = std::is_same_v<Value, words::NounMorphology>
                                  ? "noun"
                                  : "pronoun";
                output.declension = value.declension;
                output.variant = value.variant;
                output.grammatical_case = case_name(value.grammatical_case);
                output.number = number_name(value.number);
                output.gender = gender_name(value.gender);
            } else if constexpr (std::is_same_v<Value,
                                                words::AdjectiveMorphology>) {
                output.kind = "adjective";
                output.declension = value.declension;
                output.variant = value.variant;
                output.grammatical_case = case_name(value.grammatical_case);
                output.number = number_name(value.number);
                output.gender = gender_name(value.gender);
                output.degree = degree_name(value.degree);
            } else if constexpr (std::is_same_v<Value,
                                                words::NumeralMorphology>) {
                output.kind = "numeral";
                output.declension = value.declension;
                output.variant = value.variant;
                output.grammatical_case = case_name(value.grammatical_case);
                output.number = number_name(value.number);
                output.gender = gender_name(value.gender);
                output.numeral_type = numeral_type_name(value.numeral_type);
            } else if constexpr (std::is_same_v<Value,
                                                words::AdverbMorphology>) {
                output.kind = "adverb";
                output.degree = degree_name(value.degree);
            } else if constexpr (std::is_same_v<Value,
                                                words::VerbMorphology>) {
                output.kind = "verb";
                output.conjugation = value.conjugation;
                output.variant = value.variant;
                output.tense = tense_name(value.tense);
                output.voice = voice_name(value.voice);
                output.mood = mood_name(value.mood);
                output.person = value.person;
                output.number = number_name(value.number);
            } else if constexpr (std::is_same_v<Value,
                                                words::ParticipleMorphology>) {
                output.kind = "participle";
                output.conjugation = value.conjugation;
                output.variant = value.variant;
                output.grammatical_case = case_name(value.grammatical_case);
                output.number = number_name(value.number);
                output.gender = gender_name(value.gender);
                output.tense = tense_name(value.tense);
                output.voice = voice_name(value.voice);
            } else if constexpr (std::is_same_v<Value,
                                                words::SupineMorphology>) {
                output.kind = "supine";
                output.conjugation = value.conjugation;
                output.variant = value.variant;
                output.grammatical_case = case_name(value.grammatical_case);
                output.number = number_name(value.number);
                output.gender = gender_name(value.gender);
            } else if constexpr (std::is_same_v<
                                     Value, words::PrepositionMorphology>) {
                output.kind = "preposition";
                output.governs = case_name(value.governs);
            } else if constexpr (std::is_same_v<
                                     Value, words::InvariableMorphology>) {
                if (lexical_part != words::PartOfSpeech::conjunction &&
                    lexical_part != words::PartOfSpeech::interjection) {
                    throw std::logic_error{
                        "invariable morphology has an invalid lexical class"};
                }
                output.kind = lexical_part == words::PartOfSpeech::interjection
                                  ? "interjection"
                                  : "conjunction";
            } else {
                static_assert(sizeof(Value) == 0U,
                              "new Morphology variant requires browser projection");
            }
        },
        morphology);
    return output;
}

[[nodiscard]] BrowserLexicalFlags
browser_lexical_flags(const words::LexemeRecord &lexeme) {
    BrowserLexicalFlags flags;
    flags.dictionary = lexeme.dictionary == words::DictionaryKind::unique
                           ? "unique"
                           : "general";
    flags.entry_id = lexeme.dictionary_entry + 1U;
    flags.part_of_speech = lexical_part_name(lexeme.part_of_speech);
    flags.age = age_name(lexeme.age);
    flags.subject = subject_name(lexeme.subject);
    flags.geography = geography_name(lexeme.geography);
    flags.frequency = lexical_frequency_name(lexeme.frequency);
    flags.source = source_name(lexeme.source);

    switch (std::to_underlying(lexeme.part_of_speech)) {
    case std::to_underlying(words::PartOfSpeech::noun):
        flags.declension = lexeme.declension;
        flags.variant = lexeme.variant;
        flags.gender = gender_name(lexeme.gender);
        flags.noun_kind = noun_kind_name(lexeme.noun_kind);
        break;
    case std::to_underlying(words::PartOfSpeech::pronoun):
    case std::to_underlying(words::PartOfSpeech::pack):
        flags.declension = lexeme.declension;
        flags.variant = lexeme.variant;
        flags.pronoun_kind = pronoun_kind_name(lexeme.pronoun_kind);
        break;
    case std::to_underlying(words::PartOfSpeech::adjective):
        flags.declension = lexeme.declension;
        flags.variant = lexeme.variant;
        flags.degree = degree_name(lexeme.adjective_degree);
        break;
    case std::to_underlying(words::PartOfSpeech::numeral):
        flags.declension = lexeme.declension;
        flags.variant = lexeme.variant;
        flags.numeral_type = numeral_type_name(lexeme.numeral_type);
        flags.numeral_value = lexeme.numeral_value;
        break;
    case std::to_underlying(words::PartOfSpeech::adverb):
        flags.degree = degree_name(lexeme.adverb_degree);
        break;
    case std::to_underlying(words::PartOfSpeech::verb):
        flags.conjugation = lexeme.declension;
        flags.variant = lexeme.variant;
        flags.verb_kind = verb_kind_name(lexeme.verb_kind);
        break;
    case std::to_underlying(words::PartOfSpeech::preposition):
        flags.governs = case_name(lexeme.governs);
        break;
    default:
        break;
    }
    return flags;
}

[[nodiscard]] BrowserDerivationStep
browser_addon_step(const words::Database &database, const words::AddonId id,
                   const bool include_meaning, const std::string_view target) {
    BrowserDerivationStep step;
    step.kind = "addon";
    step.target = target;
    step.id = id.value();
    const auto kind = database.addon_kind(id);
    step.type = addon_kind_name(kind);
    if (kind == words::AddonKind::prefix ||
        kind == words::AddonKind::tickon) {
        const auto &prefix = database.prefix(id);
        step.text = database.prefix_string(prefix.fix);
        if (include_meaning) {
            step.has_meaning = true;
            step.meaning = database.prefix_meaning(prefix.meaning);
        }
    } else if (kind == words::AddonKind::suffix) {
        const auto &suffix = database.suffix(id);
        step.text = database.suffix_string(suffix.fix);
        if (include_meaning) {
            step.has_meaning = true;
            step.meaning = database.suffix_meaning(suffix.meaning);
        }
    } else {
        const auto &tackon = database.tackon(id);
        step.text = database.tackon_string(tackon.fix);
        if (include_meaning) {
            step.has_meaning = true;
            step.meaning = database.tackon_meaning(tackon.meaning);
        }
    }
    return step;
}

[[nodiscard]] BrowserDerivationStep
browser_rewrite_step(const words::Database &database,
                     const words::RewriteId id,
                     const bool include_meaning,
                     const std::string_view target) {
    const auto &rewrite = database.rewrite(id);
    BrowserDerivationStep step;
    step.kind = "rewrite";
    step.target = target;
    step.id = id.value();
    step.type = rewrite_kind_name(rewrite.kind);
    step.rule = database.rewrite_string(rewrite.name);
    step.before = database.rewrite_string(rewrite.before);
    step.after = database.rewrite_string(rewrite.after);
    if (include_meaning) {
        step.has_meaning = true;
        step.meaning = database.rewrite_meaning(rewrite.meaning);
    }
    return step;
}

[[nodiscard]] BrowserDerivation
browser_derivation(const words::Database &database,
                   const words::DerivationIR &derivation,
                   const words::DictionaryKind dictionary,
                   const bool include_meaning,
                   const std::string_view method_override = {},
                   const std::string_view target = "form") {
    BrowserDerivation output;
    const auto addons = derivation.steps();
    const auto leading = derivation.rewritten_form
                             ? std::min<std::size_t>(
                                   derivation.rewritten_form->leading_addon_count,
                                   addons.size())
                             : 0U;
    output.steps.reserve(addons.size() +
                         (derivation.rewritten_form
                              ? derivation.rewritten_form->steps().size()
                              : 0U));
    for (const auto id : addons.first(leading)) {
        output.steps.push_back(
            browser_addon_step(database, id, include_meaning, target));
    }
    if (derivation.rewritten_form) {
        for (const auto id : derivation.rewritten_form->steps()) {
            output.steps.push_back(
                browser_rewrite_step(database, id, include_meaning, target));
        }
    }
    for (const auto id : addons.subspan(leading)) {
        output.steps.push_back(
            browser_addon_step(database, id, include_meaning, target));
    }

    if (!method_override.empty()) {
        output.method = method_override;
    } else if (derivation.rewritten_form &&
               !derivation.rewritten_form->steps().empty()) {
        output.method = rewrite_kind_name(
            database.rewrite(derivation.rewritten_form->steps().front()).kind);
    } else if (dictionary == words::DictionaryKind::unique) {
        output.method = "unique";
    } else {
        output.method = derivation.count == 0U ? "regular" : "derived";
    }
    return output;
}

[[nodiscard]] BrowserForm
browser_form(const words::SurfaceForm &surface,
             const words::AnalysisIR &analysis) {
    BrowserForm form;
    form.has_stem_key = analysis.stem_key != 0U;
    form.stem_key = analysis.stem_key;
    form.stem = analysis.derivation.rewritten_form
                    ? analysis.derivation.rewritten_form->stem
                    : std::string{surface.slice(analysis.stem)};
    form.ending = analysis.derivation.rewritten_form
                      ? analysis.derivation.rewritten_form->ending
                      : std::string{surface.slice(analysis.ending)};
    form.recognized = form.stem + form.ending;
    return form;
}

[[nodiscard]] BrowserSearchHit
browser_hit(const words::Database &database, const words::SurfaceForm &surface,
            const words::AnalysisIR &analysis, const bool include_meaning) {
    BrowserSearchHit hit;
    hit.lexeme_id = analysis.lexeme.value();
    hit.has_rule = analysis.rule.has_value();
    if (analysis.rule) {
        hit.rule_id = analysis.rule->value();
        const auto &rule = database.rule(*analysis.rule);
        hit.rule = BrowserRuleFlags{
            true, std::string{age_name(rule.age)},
            std::string{rule_frequency_name(rule.frequency)}};
    }
    const auto &lexeme = database.lexeme(analysis.lexeme);
    hit.lemma = words::citation_lemma(database, lexeme, surface.normalized_nfc);
    if (include_meaning) {
        hit.has_meaning = true;
        hit.meaning = normalized_meaning(database.meaning(lexeme.meaning));
    }
    hit.morphology = browser_morphology(analysis.morphology,
                                        lexeme.part_of_speech);
    hit.part_of_speech = hit.morphology.kind;
    hit.form = browser_form(surface, analysis);
    hit.quantity_match = quantity_match_name(analysis.quantity_match);
    hit.lexical = browser_lexical_flags(lexeme);
    hit.derivation = browser_derivation(database, analysis.derivation,
                                        lexeme.dictionary, include_meaning);
    return hit;
}

[[nodiscard]] BrowserSearchHit
browser_hit(const words::Database &database, const words::SurfaceForm &surface,
            const words::CompoundAnalysisIR &analysis,
            const bool include_meaning,
            const std::string_view recognized) {
    BrowserSearchHit hit;
    hit.kind = "compound";
    hit.lexeme_id = analysis.lexeme.value();
    hit.has_rule = analysis.source_rule.has_value();
    if (analysis.source_rule) {
        hit.rule_id = analysis.source_rule->value();
        const auto &rule = database.rule(*analysis.source_rule);
        hit.rule = BrowserRuleFlags{
            true, std::string{age_name(rule.age)},
            std::string{rule_frequency_name(rule.frequency)}};
    }
    const auto &lexeme = database.lexeme(analysis.lexeme);
    hit.lemma = words::citation_lemma(database, lexeme, surface.normalized_nfc);
    if (include_meaning) {
        hit.has_meaning = true;
        hit.meaning = normalized_meaning(database.meaning(lexeme.meaning));
    }
    hit.morphology = browser_morphology(analysis.morphology,
                                        words::PartOfSpeech::verb);
    hit.part_of_speech = "verb";
    hit.lexical = browser_lexical_flags(lexeme);
    hit.form.stem = analysis.kind == words::CompoundKind::iri ? "SUPINE + "
                                                               : "PPL+";
    hit.form.stem.append(analysis.auxiliary);
    hit.form.recognized = recognized;
    hit.derivation = browser_derivation(
        database, analysis.source_derivation, lexeme.dictionary,
        include_meaning, "compound", "source");
    auto auxiliary_derivation = browser_derivation(
        database, analysis.auxiliary_derivation,
        words::DictionaryKind::general, include_meaning, {}, "auxiliary");
    hit.derivation.steps.insert(
        hit.derivation.steps.end(),
        std::make_move_iterator(auxiliary_derivation.steps.begin()),
        std::make_move_iterator(auxiliary_derivation.steps.end()));
    hit.compound = true;
    hit.compound_construction = compound_kind_name(analysis.kind);
    hit.compound_auxiliary = analysis.auxiliary;
    hit.compound_source_tense = tense_name(analysis.source_tense);
    hit.compound_source_voice = voice_name(analysis.source_voice);
    return hit;
}

[[nodiscard]] bool search_hit_less(const BrowserSearchHit &left,
                                   const BrowserSearchHit &right) {
    if (left.has_lexeme != right.has_lexeme) {
        return left.has_lexeme;
    }
    return left < right;
}

void canonicalize_hits(std::vector<BrowserSearchHit> &hits) {
    std::ranges::sort(hits, search_hit_less);
    const auto unique = std::ranges::unique(hits).begin();
    hits.erase(unique, hits.end());
}

[[nodiscard]] BrowserSearchResult
browser_search_result(const words::Engine &engine,
                      const words::QueryResult &result,
                      const bool include_meanings) {
    BrowserSearchResult output;
    if (include_meanings) {
        output.schema = "whitakers-words.browser-analysis";
    }
    output.dataset_id = engine.dataset_id();
    output.query.text = result.multi_token_query
                            ? result.multi_token_query->original_utf8
                            : result.surface.original_utf8;
    output.query.normalized = result.multi_token_query
                                  ? result.multi_token_query->normalized_nfc
                                  : result.surface.normalized_nfc;
    output.status = status_name(result.status);

    if (result.status == words::QueryStatus::analyzed) {
        output.hits.reserve(result.analyses.size() +
                            result.compound_analyses.size() +
                            result.artificial_analyses.size());
        for (const auto &analysis : result.analyses) {
            output.hits.push_back(
                browser_hit(engine.database(), result.surface, analysis,
                            include_meanings));
        }
        for (const auto &analysis : result.compound_analyses) {
            output.hits.push_back(
                browser_hit(engine.database(), result.surface, analysis,
                            include_meanings, output.query.normalized));
        }
        for (const auto &artificial : result.artificial_analyses) {
            std::visit(
                [&](const auto &analysis) {
                    BrowserSearchHit hit;
                    hit.has_lexeme = false;
                    hit.kind = "artificial";
                    hit.part_of_speech = "numeral";
                    hit.morphology.kind = "numeral";
                    hit.morphology.declension = 2U;
                    hit.morphology.numeral_type = "cardinal";
                    hit.form.stem = result.surface.slice(analysis.stem);
                    hit.form.recognized = hit.form.stem;
                    hit.derivation = browser_derivation(
                        engine.database(), analysis.derivation,
                        words::DictionaryKind::general, include_meanings,
                        "roman-numeral");
                    hit.artificial = true;
                    hit.artificial_method = "roman-numeral";
                    hit.artificial_value = analysis.value;
                    hit.artificial_well_formed = analysis.well_formed;
                    output.hits.push_back(std::move(hit));
                },
                artificial);
        }
        canonicalize_hits(output.hits);
    }

    output.diagnostics.reserve(result.diagnostics.size());
    std::ranges::transform(
        result.diagnostics, std::back_inserter(output.diagnostics),
        [](const words::Diagnostic &diagnostic) {
            return BrowserDiagnostic{diagnostic.code, diagnostic.severity,
                                     diagnostic.part_of_speech};
        });

    if (result.two_word_suggestion) {
        BrowserSearchSuggestion suggestion;
        suggestion.split_at = result.two_word_suggestion->logical_split;
        suggestion.classification =
            result.two_word_suggestion->both_contain_numeral
                ? "number-pair"
                : "unconstrained";
        suggestion.segments.reserve(
            result.two_word_suggestion->segments.size());
        for (const auto &segment : result.two_word_suggestion->segments) {
            BrowserSearchSegment projected;
            projected.text = segment.surface.normalized_nfc;
            projected.hits.reserve(segment.analyses.size());
            for (const auto &analysis : segment.analyses) {
                projected.hits.push_back(
                    browser_hit(engine.database(), segment.surface, analysis,
                                include_meanings));
            }
            canonicalize_hits(projected.hits);
            suggestion.segments.push_back(std::move(projected));
        }
        output.suggestions.push_back(std::move(suggestion));
    }
    return output;
}

class BrowserAnalysisEngine final {
  public:
    [[nodiscard]] LoadResult load_database(emscripten::val bytes,
                                           std::string dataset_id) {
        try {
            const auto uint8_array = emscripten::val::global("Uint8Array");
            if (!bytes.instanceof (uint8_array)) {
                return failure("invalid-database-buffer",
                               "database must be a Uint8Array", 0U);
            }

            const auto byte_length = bytes["byteLength"].as<double>();
            if (byte_length < 0.0 ||
                byte_length >
                    static_cast<double>(std::numeric_limits<std::uint32_t>::max())) {
                return failure("database-too-large",
                               "database exceeds the WebAssembly API limit", 0U);
            }
            const auto size = static_cast<std::uint32_t>(byte_length);
            std::vector<std::byte> database_image(size);
            if (!database_image.empty()) {
                auto destination = emscripten::val(emscripten::typed_memory_view(
                    database_image.size(),
                    reinterpret_cast<std::uint8_t *>(database_image.data())));
                destination.call<void>("set", bytes);
            }

            auto candidate = words::Engine::create(
                std::move(database_image),
                words::EngineConfig{std::move(dataset_id)});
            if (!candidate) {
                return failure(candidate.error().code,
                               candidate.error().message, size);
            }

            // WHY: replacement happens only after complete WWDB validation,
            // so a failed reload cannot destroy a working browser session.
            engine_ = std::move(*candidate);
            database_bytes_ = size;
            return LoadResult{true, {}, {}, size, database_kind()};
        } catch (const std::bad_alloc &) {
            return failure("out-of-memory",
                           "not enough WebAssembly memory for the database",
                           0U);
        } catch (const std::exception &error) {
            return failure("database-load-failure", error.what(), 0U);
        }
    }

    [[nodiscard]] bool ready() const noexcept { return engine_ != nullptr; }

    [[nodiscard]] std::string dataset_id() const {
        require_ready();
        return std::string{engine_->dataset_id()};
    }

    [[nodiscard]] std::uint32_t database_bytes() const noexcept {
        return database_bytes_;
    }

    [[nodiscard]] std::string database_kind() const {
        require_ready();
        return std::string{
            engine_->database().content() == words::DatabaseContent::full
                ? full_database_kind
                : search_database_kind};
    }

    [[nodiscard]] BrowserSearchResult analyze(const std::string &utf8,
                                              const bool two_words) const {
        require_ready();
        if (!engine_->supports_full_analysis()) {
            throw std::logic_error{
                "analysis requires a full WWDB with meanings"};
        }
        const auto options = analysis_options(two_words);
        const auto result = engine_->analyze_text(utf8, options);
        return browser_search_result(*engine_, result, true);
    }

    [[nodiscard]] BrowserSearchResult search(const std::string &utf8,
                                             const bool two_words) const {
        require_ready();
        const auto options = analysis_options(two_words);
        const auto result = engine_->analyze_text(utf8, options);
        return browser_search_result(*engine_, result, false);
    }

    void reset() noexcept {
        engine_.reset();
        database_bytes_ = 0U;
    }

  private:
    [[nodiscard]] static LoadResult failure(std::string code,
                                            std::string message,
                                            const std::uint32_t size) {
        return LoadResult{false, std::move(code), std::move(message), size,
                          {}};
    }

    [[nodiscard]] static words::AnalysisOptions
    analysis_options(const bool two_words) noexcept {
        return words::AnalysisOptions{
            two_words ? words::TwoWordsMode::legacy_first_match
                      : words::TwoWordsMode::disabled};
    }

    void require_ready() const {
        if (!engine_) {
            // WHY: querying an unloaded wrapper is API misuse rather than a
            // Latin diagnostic and must not masquerade as analysis-v1 data.
            throw std::logic_error("analysis engine has no loaded database");
        }
    }

    std::unique_ptr<const words::Engine> engine_;
    std::uint32_t database_bytes_{};
};

} // namespace

EMSCRIPTEN_BINDINGS(words_analysis_engine) {
    // WHY: registered vectors keep the WebAssembly boundary typed and let the
    // high-level wrapper copy-and-release their Embind handles explicitly.
    emscripten::register_vector<std::string>("VectorString");
    emscripten::register_vector<std::uint32_t>("VectorUint32");

    emscripten::value_object<LoadResult>("LoadResult")
        .field("ok", &LoadResult::ok)
        .field("code", &LoadResult::code)
        .field("message", &LoadResult::message)
        .field("databaseBytes", &LoadResult::database_bytes)
        .field("databaseKind", &LoadResult::database_kind);

    emscripten::value_object<BrowserQuery>("SearchQuery")
        .field("text", &BrowserQuery::text)
        .field("normalized", &BrowserQuery::normalized)
        .field("mode", &BrowserQuery::mode);

    emscripten::value_object<BrowserDiagnostic>("SearchDiagnostic")
        .field("code", &BrowserDiagnostic::code)
        .field("severity", &BrowserDiagnostic::severity)
        .field("partOfSpeech", &BrowserDiagnostic::part_of_speech);

    emscripten::value_object<BrowserMorphology>("SearchMorphology")
        .field("kind", &BrowserMorphology::kind)
        .field("declension", &BrowserMorphology::declension)
        .field("conjugation", &BrowserMorphology::conjugation)
        .field("variant", &BrowserMorphology::variant)
        .field("case", &BrowserMorphology::grammatical_case)
        .field("number", &BrowserMorphology::number)
        .field("gender", &BrowserMorphology::gender)
        .field("degree", &BrowserMorphology::degree)
        .field("numeralType", &BrowserMorphology::numeral_type)
        .field("tense", &BrowserMorphology::tense)
        .field("voice", &BrowserMorphology::voice)
        .field("mood", &BrowserMorphology::mood)
        .field("person", &BrowserMorphology::person)
        .field("governs", &BrowserMorphology::governs);

    emscripten::value_object<BrowserLexicalFlags>("SearchLexicalFlags")
        .field("dictionary", &BrowserLexicalFlags::dictionary)
        .field("entryId", &BrowserLexicalFlags::entry_id)
        .field("partOfSpeech", &BrowserLexicalFlags::part_of_speech)
        .field("declension", &BrowserLexicalFlags::declension)
        .field("conjugation", &BrowserLexicalFlags::conjugation)
        .field("variant", &BrowserLexicalFlags::variant)
        .field("gender", &BrowserLexicalFlags::gender)
        .field("nounKind", &BrowserLexicalFlags::noun_kind)
        .field("pronounKind", &BrowserLexicalFlags::pronoun_kind)
        .field("degree", &BrowserLexicalFlags::degree)
        .field("numeralType", &BrowserLexicalFlags::numeral_type)
        .field("numeralValue", &BrowserLexicalFlags::numeral_value)
        .field("verbKind", &BrowserLexicalFlags::verb_kind)
        .field("governs", &BrowserLexicalFlags::governs)
        .field("age", &BrowserLexicalFlags::age)
        .field("subject", &BrowserLexicalFlags::subject)
        .field("geography", &BrowserLexicalFlags::geography)
        .field("frequency", &BrowserLexicalFlags::frequency)
        .field("source", &BrowserLexicalFlags::source);

    emscripten::value_object<BrowserRuleFlags>("SearchRuleFlags")
        .field("present", &BrowserRuleFlags::present)
        .field("age", &BrowserRuleFlags::age)
        .field("frequency", &BrowserRuleFlags::frequency);

    emscripten::value_object<BrowserForm>("SearchForm")
        .field("stem", &BrowserForm::stem)
        .field("hasStemKey", &BrowserForm::has_stem_key)
        .field("stemKey", &BrowserForm::stem_key)
        .field("ending", &BrowserForm::ending)
        .field("recognized", &BrowserForm::recognized);

    emscripten::value_object<BrowserDerivationStep>("SearchDerivationStep")
        .field("kind", &BrowserDerivationStep::kind)
        .field("target", &BrowserDerivationStep::target)
        .field("id", &BrowserDerivationStep::id)
        .field("type", &BrowserDerivationStep::type)
        .field("text", &BrowserDerivationStep::text)
        .field("rule", &BrowserDerivationStep::rule)
        .field("before", &BrowserDerivationStep::before)
        .field("after", &BrowserDerivationStep::after)
        .field("hasMeaning", &BrowserDerivationStep::has_meaning)
        .field("meaning", &BrowserDerivationStep::meaning);
    emscripten::register_vector<BrowserDerivationStep>(
        "VectorSearchDerivationStep");

    emscripten::value_object<BrowserDerivation>("SearchDerivation")
        .field("method", &BrowserDerivation::method)
        .field("steps", &BrowserDerivation::steps);

    emscripten::value_object<BrowserSearchHit>("ResolvedSearchHit")
        .field("hasLexeme", &BrowserSearchHit::has_lexeme)
        .field("kind", &BrowserSearchHit::kind)
        .field("lexemeId", &BrowserSearchHit::lexeme_id)
        .field("hasRule", &BrowserSearchHit::has_rule)
        .field("ruleId", &BrowserSearchHit::rule_id)
        .field("lemma", &BrowserSearchHit::lemma)
        .field("hasMeaning", &BrowserSearchHit::has_meaning)
        .field("meaning", &BrowserSearchHit::meaning)
        .field("partOfSpeech", &BrowserSearchHit::part_of_speech)
        .field("form", &BrowserSearchHit::form)
        .field("quantityMatch", &BrowserSearchHit::quantity_match)
        .field("morphology", &BrowserSearchHit::morphology)
        .field("lexical", &BrowserSearchHit::lexical)
        .field("rule", &BrowserSearchHit::rule)
        .field("derivation", &BrowserSearchHit::derivation)
        .field("compound", &BrowserSearchHit::compound)
        .field("compoundConstruction",
               &BrowserSearchHit::compound_construction)
        .field("compoundAuxiliary", &BrowserSearchHit::compound_auxiliary)
        .field("compoundSourceTense",
               &BrowserSearchHit::compound_source_tense)
        .field("compoundSourceVoice",
               &BrowserSearchHit::compound_source_voice)
        .field("artificial", &BrowserSearchHit::artificial)
        .field("artificialMethod", &BrowserSearchHit::artificial_method)
        .field("artificialValue", &BrowserSearchHit::artificial_value)
        .field("artificialWellFormed",
               &BrowserSearchHit::artificial_well_formed);
    emscripten::register_vector<BrowserSearchHit>("VectorResolvedSearchHit");

    emscripten::value_object<BrowserSearchSegment>("ResolvedSearchSegment")
        .field("text", &BrowserSearchSegment::text)
        .field("hits", &BrowserSearchSegment::hits);
    emscripten::register_vector<BrowserSearchSegment>(
        "VectorResolvedSearchSegment");

    emscripten::value_object<BrowserSearchSuggestion>(
        "ResolvedSearchSuggestion")
        .field("method", &BrowserSearchSuggestion::method)
        .field("splitAt", &BrowserSearchSuggestion::split_at)
        .field("classification", &BrowserSearchSuggestion::classification)
        .field("segments", &BrowserSearchSuggestion::segments);
    emscripten::register_vector<BrowserSearchSuggestion>(
        "VectorResolvedSearchSuggestion");
    emscripten::register_vector<BrowserDiagnostic>("VectorSearchDiagnostic");

    emscripten::value_object<BrowserSearchResult>("ResolvedSearchResult")
        .field("schema", &BrowserSearchResult::schema)
        .field("schemaVersion", &BrowserSearchResult::schema_version)
        .field("datasetId", &BrowserSearchResult::dataset_id)
        .field("query", &BrowserSearchResult::query)
        .field("status", &BrowserSearchResult::status)
        .field("hits", &BrowserSearchResult::hits)
        .field("diagnostics", &BrowserSearchResult::diagnostics)
        .field("suggestions", &BrowserSearchResult::suggestions);

    emscripten::class_<BrowserAnalysisEngine>("AnalysisEngine")
        .constructor<>()
        .function("loadDatabase", &BrowserAnalysisEngine::load_database)
        .function("ready", &BrowserAnalysisEngine::ready)
        .function("datasetId", &BrowserAnalysisEngine::dataset_id)
        .function("databaseBytes", &BrowserAnalysisEngine::database_bytes)
        .function("databaseKind", &BrowserAnalysisEngine::database_kind)
        .function("analyze", &BrowserAnalysisEngine::analyze)
        .function("search", &BrowserAnalysisEngine::search)
        .function("reset", &BrowserAnalysisEngine::reset);
}
