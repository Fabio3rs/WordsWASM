#include "test_support.hpp"

#include "words/json.hpp"
#include "words/lexeme.hpp"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <compare>
#include <cstdint>
#include <ranges>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace words {

using Json = nlohmann::ordered_json;

namespace {

struct NominalExpectation final {
    std::uint32_t dictionary_entry{};
    PartOfSpeech part_of_speech{PartOfSpeech::unknown};
    std::uint8_t declension{};
    std::uint8_t variant{};
    GrammaticalCase grammatical_case{GrammaticalCase::unknown};
    GrammaticalNumber number{GrammaticalNumber::unknown};
    Gender gender{Gender::unknown};
    Degree degree{Degree::unknown};
    std::uint8_t stem_key{};
    auto operator<=>(const NominalExpectation &) const = default;
};

struct AnalysisSemanticSignature final {
    LexemeId lexeme;
    std::optional<RuleId> rule;
    std::uint8_t stem_key{};
    SurfaceRange stem;
    SurfaceRange ending;
    Morphology morphology;
    QuantityMatch quantity_match{QuantityMatch::unspecified};
    DerivationIR derivation;
    MorphologicalAssessmentIR assessment;
    auto operator<=>(const AnalysisSemanticSignature &) const = default;
};

[[nodiscard]] std::vector<AnalysisSemanticSignature>
semantic_signatures(const std::span<const AnalysisIR> analyses) {
    std::vector<AnalysisSemanticSignature> signatures;
    signatures.reserve(analyses.size());
    for (const auto &analysis : analyses) {
        signatures.push_back({
            .lexeme = analysis.lexeme,
            .rule = analysis.rule,
            .stem_key = analysis.stem_key,
            .stem = analysis.stem,
            .ending = analysis.ending,
            .morphology = analysis.morphology,
            .quantity_match = analysis.quantity_match,
            .derivation = analysis.derivation,
            .assessment = analysis.assessment,
        });
    }
    std::ranges::sort(signatures);
    return signatures;
}

[[nodiscard]] constexpr NominalExpectation
noun_expectation(const std::uint32_t dictionary_entry,
                 const std::uint8_t declension, const std::uint8_t variant,
                 const GrammaticalCase grammatical_case,
                 const GrammaticalNumber number, const Gender gender,
                 const std::uint8_t stem_key) noexcept {
    return NominalExpectation{
        .dictionary_entry = dictionary_entry,
        .part_of_speech = PartOfSpeech::noun,
        .declension = declension,
        .variant = variant,
        .grammatical_case = grammatical_case,
        .number = number,
        .gender = gender,
        .degree = Degree::unknown,
        .stem_key = stem_key,
    };
}

[[nodiscard]] constexpr NominalExpectation adjective_expectation(
    const std::uint32_t dictionary_entry, const std::uint8_t declension,
    const std::uint8_t variant, const GrammaticalCase grammatical_case,
    const GrammaticalNumber number, const Gender gender, const Degree degree,
    const std::uint8_t stem_key) noexcept {
    return NominalExpectation{
        .dictionary_entry = dictionary_entry,
        .part_of_speech = PartOfSpeech::adjective,
        .declension = declension,
        .variant = variant,
        .grammatical_case = grammatical_case,
        .number = number,
        .gender = gender,
        .degree = degree,
        .stem_key = stem_key,
    };
}

void expect_nominal_analyses(const std::string_view surface,
                             const std::span<const NominalExpectation> expected,
                             const QuantityMatch quantity_match,
                             const std::string_view expected_stem,
                             const std::string_view expected_ending) {
    const auto result = test::engine().analyze(surface);
    ASSERT_EQ(result.status, QueryStatus::analyzed) << surface;
    EXPECT_EQ(result.surface.normalized_nfc, surface);
    ASSERT_EQ(result.analyses.size(), expected.size()) << surface;

    const auto &database = test::engine().database();
    std::vector<NominalExpectation> actual;
    actual.reserve(result.analyses.size());
    for (std::size_t index = 0; index < result.analyses.size(); ++index) {
        const auto &analysis = result.analyses[index];
        const auto &lexeme = database.lexeme(analysis.lexeme);

        EXPECT_EQ(analysis.quantity_match, quantity_match)
            << surface << " analysis " << index;
        EXPECT_EQ(result.surface.slice(analysis.stem), expected_stem)
            << surface << " analysis " << index;
        EXPECT_EQ(result.surface.slice(analysis.ending), expected_ending)
            << surface << " analysis " << index;
        EXPECT_EQ(analysis.derivation.count, 0U)
            << surface << " analysis " << index;

        if (const auto *noun =
                std::get_if<NounMorphology>(&analysis.morphology)) {
            actual.push_back(noun_expectation(
                lexeme.dictionary_entry + 1U, noun->declension, noun->variant,
                noun->grammatical_case, noun->number, noun->gender,
                analysis.stem_key));
        } else if (const auto *adjective =
                       std::get_if<AdjectiveMorphology>(&analysis.morphology)) {
            actual.push_back(adjective_expectation(
                lexeme.dictionary_entry + 1U, adjective->declension,
                adjective->variant, adjective->grammatical_case,
                adjective->number, adjective->gender, adjective->degree,
                analysis.stem_key));
        } else {
            ADD_FAILURE() << surface << " analysis " << index
                          << " is not nominal";
            continue;
        }

        EXPECT_EQ(lexeme.part_of_speech, actual.back().part_of_speech)
            << surface << " analysis " << index;
        ASSERT_TRUE(analysis.rule.has_value())
            << surface << " analysis " << index;
        const auto &rule = database.rule(*analysis.rule);
        EXPECT_EQ(rule.part_of_speech, actual.back().part_of_speech)
            << surface << " analysis " << index;
        EXPECT_EQ(rule.stem_key, analysis.stem_key)
            << surface << " analysis " << index;
    }

    auto sorted_expected =
        std::vector<NominalExpectation>{expected.begin(), expected.end()};
    std::ranges::sort(actual);
    std::ranges::sort(sorted_expected);
    ASSERT_EQ(actual.size(), sorted_expected.size()) << surface;
    for (std::size_t index = 0; index < sorted_expected.size(); ++index) {
        const auto &found = actual[index];
        const auto &item = sorted_expected[index];
        EXPECT_EQ(found.dictionary_entry, item.dictionary_entry);
        EXPECT_EQ(found.part_of_speech, item.part_of_speech);
        EXPECT_EQ(found.declension, item.declension);
        EXPECT_EQ(found.variant, item.variant);
        EXPECT_EQ(found.grammatical_case, item.grammatical_case);
        EXPECT_EQ(found.number, item.number);
        EXPECT_EQ(found.gender, item.gender);
        EXPECT_EQ(found.degree, item.degree);
        EXPECT_EQ(found.stem_key, item.stem_key);
    }
}

} // namespace

TEST(EngineTest, SearchDatabasePreservesTheFullDatabaseHitContract) {
    constexpr std::array<std::string_view, 10> fixtures{
        "puella", "anaticulus", "mālum", "eadem",     "mavisque",
        "pretor", "amasti",     "iv",    "amata est", "anaticuliculiculus",
    };
    ASSERT_TRUE(test::engine().supports_full_analysis());
    ASSERT_FALSE(test::search_engine().supports_full_analysis());

    for (const auto fixture : fixtures) {
        const auto full_result = test::engine().analyze_text(fixture);
        const auto search_result = test::search_engine().analyze_text(fixture);
        EXPECT_EQ(
            Json::parse(search_json(test::engine(), full_result)),
            Json::parse(search_json(test::search_engine(), search_result)))
            << fixture;
    }

    const auto result = test::search_engine().analyze("puella");
    EXPECT_THROW(
        static_cast<void>(analysis_json(test::search_engine(), result)),
        std::logic_error);
}

TEST(EngineTest, UsesCompactDatasetTagForResultOwnership) {
    const auto result = test::engine().analyze("puella");
    EXPECT_TRUE(test::engine().owns(result));
    EXPECT_TRUE(test::search_engine().owns(result));
    EXPECT_FALSE(test::engine().owns(QueryResult{}));

    auto other_dataset_id = std::string{test::dataset_id};
    other_dataset_id.back() = other_dataset_id.back() == '0' ? '1' : '0';
    auto loaded = Engine::create(test::read_database(),
                                 EngineConfig{std::string{other_dataset_id}});
    ASSERT_TRUE(loaded);

    EXPECT_EQ((**loaded).dataset_id(), other_dataset_id);
    EXPECT_FALSE((**loaded).owns(result));
    EXPECT_THROW(static_cast<void>(analysis_json(**loaded, result)),
                 std::logic_error);
    EXPECT_THROW(static_cast<void>(search_json(**loaded, result)),
                 std::logic_error);

    const auto other_result = (**loaded).analyze("puella");
    const auto document = Json::parse(search_json(**loaded, other_result));
    EXPECT_EQ(document.at("datasetId"), other_dataset_id);
}

TEST(EngineTest, AllowsAnEmptyDatasetIdAsAnonymousMode) {
    auto first = Engine::create(test::read_database());
    auto second = Engine::create(test::read_database(), EngineConfig{});
    ASSERT_TRUE(first);
    ASSERT_TRUE(second);

    const auto anonymous_result = (**first).analyze("puella");
    EXPECT_TRUE((**first).dataset_id().empty());
    EXPECT_TRUE((**first).owns(anonymous_result));
    EXPECT_TRUE((**second).owns(anonymous_result));
    EXPECT_TRUE((**first).owns(QueryResult{}));
    EXPECT_FALSE(test::engine().owns(anonymous_result));
    EXPECT_FALSE((**first).owns(test::engine().analyze("puella")));

    const auto document = Json::parse(search_json(**first, anonymous_result));
    EXPECT_EQ(document.at("datasetId"), "");
}

TEST(EngineTest, SearchDatabaseResolvesCanonicalLemmaWithoutMeanings) {
    constexpr LexemeId amo{2870U};
    const auto &full_database = test::engine().database();
    const auto &search_database = test::search_engine().database();

    EXPECT_EQ(citation_lemma(full_database, full_database.lexeme(amo)), "amo");
    EXPECT_EQ(citation_lemma(search_database, search_database.lexeme(amo)),
              "amo");
    EXPECT_EQ(dictionary_form(full_database, full_database.lexeme(amo)),
              "amo, amare, amavi, amatus");
    const auto paradigm = lexeme_paradigm(full_database.lexeme(amo));
    ASSERT_TRUE(std::holds_alternative<ConjugationParadigm>(paradigm));
    EXPECT_EQ(std::get<ConjugationParadigm>(paradigm).number, 1U);
    EXPECT_EQ(std::get<ConjugationParadigm>(paradigm).variant, 1U);

    constexpr std::array<std::pair<std::string_view, std::string_view>, 9>
        fixtures{{
            {"puella", "puella"},
            {"servus", "servus"},
            {"rex", "rex"},
            {"bonus", "bonus"},
            {"fortis", "fortis"},
            {"bene", "bene"},
            {"cum", "cum"},
            {"et", "et"},
            {"quis", "quis"},
        }};
    for (const auto &[surface, expected] : fixtures) {
        const auto result = test::search_engine().analyze(surface);
        EXPECT_TRUE(std::ranges::any_of(result.analyses, [&](const AnalysisIR
                                                                 &analysis) {
            const auto &lexeme = search_database.lexeme(analysis.lexeme);
            return citation_lemma(search_database, lexeme, surface) == expected;
        })) << surface;
    }
}

TEST(EngineTest, FoldsIAndJAndUAndVWithoutChangingAnalysisSemantics) {
    constexpr std::array pairs{
        std::pair<std::string_view, std::string_view>{"juvenis", "iuuenis"},
        std::pair<std::string_view, std::string_view>{"mavis", "mauis"},
        std::pair<std::string_view, std::string_view>{"mavisque", "mauisque"},
    };

    for (const auto &[traditional, canonical] : pairs) {
        const auto traditional_result = test::engine().analyze(traditional);
        const auto canonical_result = test::engine().analyze(canonical);

        ASSERT_EQ(traditional_result.status, QueryStatus::analyzed)
            << traditional;
        ASSERT_EQ(canonical_result.status, QueryStatus::analyzed) << canonical;
        ASSERT_FALSE(traditional_result.analyses.empty()) << traditional;
        ASSERT_FALSE(canonical_result.analyses.empty()) << canonical;
        EXPECT_EQ(traditional_result.surface.orthography_ascii, traditional);
        EXPECT_EQ(canonical_result.surface.orthography_ascii, canonical);
        EXPECT_EQ(traditional_result.surface.lookup_ascii,
                  canonical_result.surface.lookup_ascii);
        EXPECT_EQ(canonical_result.surface.lookup_ascii, canonical);
        EXPECT_EQ(semantic_signatures(traditional_result.analyses),
                  semantic_signatures(canonical_result.analyses))
            << traditional << " / " << canonical;
        EXPECT_TRUE(traditional_result.compound_analyses.empty());
        EXPECT_TRUE(canonical_result.compound_analyses.empty());
        EXPECT_TRUE(traditional_result.artificial_analyses.empty());
        EXPECT_TRUE(canonical_result.artificial_analyses.empty());
        EXPECT_TRUE(traditional_result.diagnostics.empty());
        EXPECT_TRUE(canonical_result.diagnostics.empty());
    }
}

TEST(EngineTest, AnalyzesNounOnlyFixtures) {
    constexpr std::array<std::pair<std::string_view, std::size_t>, 6> fixtures{{
        {"puella", 3U},
        {"servus", 1U},
        {"regina", 3U},
        {"rex", 2U},
        {"manus", 6U},
        {"dies", 10U},
    }};
    for (const auto &[word, count] : fixtures) {
        const auto result = test::engine().analyze(word);
        EXPECT_EQ(result.status, QueryStatus::analyzed) << word;
        EXPECT_EQ(result.analyses.size(), count) << word;
    }
}

TEST(EngineTest, AnalyzesRegularAdjectiveFixtures) {
    constexpr std::array<std::pair<std::string_view, std::size_t>, 7> fixtures{{
        {"pulcher", 2U},
        {"bonus", 2U},
        {"maior", 10U},
        {"maximus", 4U},
        {"acer", 5U},
        {"bellum", 9U},
        {"fortis", 7U},
    }};
    for (const auto &[word, count] : fixtures) {
        const auto result = test::engine().analyze(word);
        EXPECT_EQ(result.status, QueryStatus::analyzed) << word;
        EXPECT_EQ(result.analyses.size(), count) << word;
    }
}

TEST(EngineTest, DerivesAdjectiveDegreeFromLexemeOrStemKey) {
    const auto has_degree = [](const QueryResult &result, const Degree degree) {
        return std::ranges::any_of(
            result.analyses, [degree](const AnalysisIR &analysis) {
                const auto *morphology =
                    std::get_if<AdjectiveMorphology>(&analysis.morphology);
                return morphology != nullptr && morphology->degree == degree;
            });
    };

    EXPECT_TRUE(
        has_degree(test::engine().analyze("pulcher"), Degree::positive));
    EXPECT_TRUE(
        has_degree(test::engine().analyze("maior"), Degree::comparative));
    EXPECT_TRUE(
        has_degree(test::engine().analyze("maximus"), Degree::superlative));
}

TEST(EngineTest, AnalyzesEveryRegularSemanticClass) {
    constexpr std::array<std::pair<std::string_view, std::size_t>, 11> fixtures{
        {
            {"duo", 7U},
            {"bene", 1U},
            {"amo", 1U},
            {"amans", 8U},
            {"amatum", 10U},
            {"cum", 4U},
            {"et", 1U},
            {"heu", 1U},
            {"quis", 13U},
            {"rosa", 9U},
            {"forte", 8U},
        }};
    for (const auto &[word, count] : fixtures) {
        const auto result = test::engine().analyze(word);
        EXPECT_EQ(result.status, QueryStatus::analyzed) << word;
        EXPECT_EQ(result.analyses.size(), count) << word;
        EXPECT_TRUE(result.diagnostics.empty()) << word;
    }
}

TEST(EngineTest, PreservesRealMorphologicalAmbiguity) {
    const auto result = test::engine().analyze("puellae");
    ASSERT_EQ(result.status, QueryStatus::analyzed);

    const auto has_noun_form = [&](const GrammaticalCase grammatical_case,
                                   const GrammaticalNumber number) {
        return std::ranges::any_of(
            result.analyses, [&](const AnalysisIR &analysis) {
                const auto *noun =
                    std::get_if<NounMorphology>(&analysis.morphology);
                return noun != nullptr &&
                       noun->grammatical_case == grammatical_case &&
                       noun->number == number &&
                       noun->gender == Gender::feminine;
            });
    };
    EXPECT_TRUE(
        has_noun_form(GrammaticalCase::genitive, GrammaticalNumber::singular));
    EXPECT_TRUE(
        has_noun_form(GrammaticalCase::dative, GrammaticalNumber::singular));
    EXPECT_TRUE(
        has_noun_form(GrammaticalCase::nominative, GrammaticalNumber::plural));
}

TEST(EngineTest, PreservesEveryVerbKindRepresentedByARealLexeme) {
    struct Fixture final {
        std::string_view surface;
        VerbKind kind;
    };
    constexpr std::array fixtures{
        Fixture{.surface = "sum", .kind = VerbKind::to_be},
        Fixture{.surface = "absum", .kind = VerbKind::compound_of_to_be},
        Fixture{.surface = "accredo", .kind = VerbKind::governs_dative},
        Fixture{.surface = "supersido", .kind = VerbKind::governs_ablative},
        Fixture{.surface = "abalieno", .kind = VerbKind::transitive},
        Fixture{.surface = "curro", .kind = VerbKind::intransitive},
        Fixture{.surface = "licet", .kind = VerbKind::impersonal},
        Fixture{.surface = "loquor", .kind = VerbKind::deponent},
        Fixture{.surface = "audeo", .kind = VerbKind::semideponent},
        Fixture{.surface = "odi", .kind = VerbKind::perfect_definite},
    };

    for (const auto &fixture : fixtures) {
        SCOPED_TRACE(fixture.surface);
        for (const auto *engine : {&test::engine(), &test::search_engine()}) {
            const auto result = engine->analyze(fixture.surface);
            EXPECT_TRUE(std::ranges::any_of(
                result.analyses, [&](const AnalysisIR &analysis) {
                    const auto &lexeme =
                        engine->database().lexeme(analysis.lexeme);
                    return lexeme.part_of_speech == PartOfSpeech::verb &&
                           lexeme.verb_kind == fixture.kind;
                }));
        }
    }
}

TEST(EngineTest, AnnotatesAndPreservesActiveDeponentFormsInEveryProjection) {
    constexpr std::uint32_t reor_entry = 32909U;
    const auto is_reor = [](const Engine &engine, const AnalysisIR &analysis) {
        const auto &lexeme = engine.database().lexeme(analysis.lexeme);
        return lexeme.dictionary == DictionaryKind::general &&
               lexeme.dictionary_entry + 1U == reor_entry &&
               lexeme.verb_kind == VerbKind::deponent;
    };

    const auto expects_person = [&](const Engine &engine,
                                    const std::string_view surface,
                                    const Person person) {
        const auto result = engine.analyze(surface);
        return std::ranges::any_of(
            result.analyses, [&](const AnalysisIR &analysis) {
                const auto *verb =
                    std::get_if<VerbMorphology>(&analysis.morphology);
                return is_reor(engine, analysis) && verb != nullptr &&
                       verb->tense == Tense::present &&
                       verb->voice == Voice::passive &&
                       verb->mood == Mood::indicative &&
                       verb->person == person &&
                       verb->number == GrammaticalNumber::singular;
            });
    };

    const std::array engines{&test::engine(), &test::search_engine()};
    for (const auto *engine : engines) {
        const auto res = engine->analyze("res");
        ASSERT_EQ(res.status, QueryStatus::analyzed);
        EXPECT_TRUE(
            std::ranges::any_of(res.analyses, [&](const AnalysisIR &analysis) {
                return is_reor(*engine, analysis) &&
                       std::ranges::contains(
                           analysis.assessment.whitaker_trim.values(),
                           WhitakerTrimReason::deponent_active_form);
            }));

        EXPECT_TRUE(expects_person(*engine, "reor", Person::first));
        EXPECT_TRUE(expects_person(*engine, "reris", Person::second));
    }

    const auto result = test::engine().analyze("res");
    const auto full = Json::parse(analysis_json(test::engine(), result));
    const auto search = Json::parse(search_json(test::engine(), result));
    EXPECT_EQ(full.at("analyses").size(), result.total_analyses());
    EXPECT_EQ(search.at("hits").size(), result.analyses.size());
}

TEST(EngineTest, ReproducesWhitakerTrimAsAnExplainablePolicy) {
    const auto has_reason = [](const AnalysisIR &analysis,
                               const WhitakerTrimReason reason) {
        return std::ranges::contains(analysis.assessment.whitaker_trim.values(),
                                     reason);
    };

    const auto short_imperative = test::engine().analyze("reg");
    ASSERT_EQ(short_imperative.analyses.size(), 1U);
    EXPECT_TRUE(has_reason(short_imperative.analyses.front(),
                           WhitakerTrimReason::unsupported_short_imperative));
    for (const auto *const licensed : {"dic", "duc", "fac"}) {
        const auto result = test::engine().analyze(licensed);
        EXPECT_TRUE(std::ranges::none_of(result.analyses, [&](const AnalysisIR
                                                                  &analysis) {
            return has_reason(analysis,
                              WhitakerTrimReason::unsupported_short_imperative);
        })) << licensed;
    }

    const auto impersonal = test::engine().analyze("liceo");
    EXPECT_TRUE(std::ranges::any_of(
        impersonal.analyses, [&](const AnalysisIR &analysis) {
            return has_reason(analysis,
                              WhitakerTrimReason::impersonal_non_third_person);
        }));

    const auto active_perfect = test::engine().analyze("ausi");
    EXPECT_TRUE(std::ranges::any_of(
        active_perfect.analyses, [&](const AnalysisIR &analysis) {
            return has_reason(
                analysis,
                WhitakerTrimReason::semideponent_active_perfect_system);
        }));

    EXPECT_EQ(short_imperative.status, QueryStatus::analyzed);
    EXPECT_TRUE(std::ranges::any_of(
        impersonal.analyses, [](const AnalysisIR &analysis) {
            return !analysis.assessment.whitaker_trim.accepted();
        }));
    EXPECT_TRUE(std::ranges::any_of(
        active_perfect.analyses, [&](const AnalysisIR &analysis) {
            return has_reason(
                analysis,
                WhitakerTrimReason::semideponent_active_perfect_system);
        }));
}

TEST(EngineTest, FlagsNormativeImpersonalNumberDisagreementLosslessly) {
    const auto find_impersonal = [](const QueryResult &result,
                                    const GrammaticalNumber number) {
        return std::ranges::find_if(
            result.analyses, [&](const AnalysisIR &analysis) {
                const auto &lexeme =
                    test::engine().database().lexeme(analysis.lexeme);
                const auto *verb =
                    std::get_if<VerbMorphology>(&analysis.morphology);
                return lexeme.verb_kind == VerbKind::impersonal &&
                       verb != nullptr && verb->person == Person::third &&
                       verb->number == number;
            });
    };

    const auto licent = test::engine().analyze("licent");
    const auto plural = find_impersonal(licent, GrammaticalNumber::plural);
    ASSERT_NE(plural, licent.analyses.end());
    ASSERT_TRUE(
        std::ranges::contains(plural->assessment.notice_values(),
                              MorphologicalNotice::source_disagreement));

    const auto licet = test::engine().analyze("licet");
    const auto singular = find_impersonal(licet, GrammaticalNumber::singular);
    ASSERT_NE(singular, licet.analyses.end());
    EXPECT_FALSE(
        std::ranges::contains(singular->assessment.notice_values(),
                              MorphologicalNotice::source_disagreement));
}

TEST(EngineTest, FlagsLegacyFutureActivePeriphrasticVoiceLosslessly) {
    const auto result = test::engine().analyze_text("amaturus est");
    const auto compound = std::ranges::find_if(
        result.compound_analyses, [](const CompoundAnalysisIR &analysis) {
            return analysis.source_tense == Tense::future &&
                   analysis.source_voice == Voice::active;
        });
    ASSERT_NE(compound, result.compound_analyses.end());
    EXPECT_EQ(compound->morphology.voice, Voice::passive);
    ASSERT_TRUE(
        std::ranges::contains(compound->assessment.notice_values(),
                              MorphologicalNotice::source_disagreement));

    const auto full = Json::parse(analysis_json_v2(test::engine(), result));
    const auto projection =
        std::ranges::find_if(full.at("analyses"), [](const Json &analysis) {
            return analysis.at("derivation").at("method") == "compound";
        });
    ASSERT_NE(projection, full.at("analyses").end());
    EXPECT_EQ(projection->at("assessment").at("notices").front().at("code"),
              "source-disagreement");
}

TEST(EngineTest, PreservesAudeoPassiveWithRelatedEvidenceNotices) {
    const auto result = test::engine().analyze("audetur");
    ASSERT_EQ(result.status, QueryStatus::analyzed);
    ASSERT_EQ(result.analyses.size(), 1U);
    const auto &assessment = result.analyses.front().assessment;
    EXPECT_FALSE(assessment.whitaker_trim.accepted());
    EXPECT_TRUE(std::ranges::contains(
        assessment.whitaker_trim.values(),
        WhitakerTrimReason::semideponent_passive_present_system));
    EXPECT_TRUE(std::ranges::contains(
        assessment.notice_values(),
        MorphologicalNotice::related_passive_usage_attested));
    EXPECT_TRUE(std::ranges::contains(
        assessment.notice_values(), MorphologicalNotice::source_disagreement));
    EXPECT_TRUE(
        std::ranges::contains(assessment.notice_values(),
                              MorphologicalNotice::manual_review_recommended));

    const auto json = Json::parse(analysis_json_v2(test::engine(), result));
    const auto &serialized = json.at("analyses").front().at("assessment");
    EXPECT_FALSE(serialized.at("whitakerTrim").at("compatible"));
    EXPECT_EQ(serialized.at("notices").size(), 3U);
    const auto search = Json::parse(search_json_v2(test::engine(), result));
    const auto &search_assessment = search.at("hits").front().at("assessment");
    EXPECT_FALSE(search_assessment.at("whitakerTrim").at("compatible"));
    EXPECT_EQ(search_assessment.at("notices").size(), 3U);
}

TEST(EngineTest, QualifiesDocumentedSemideponentExceptionsByAnalysis) {
    const auto has_notice = [](const AnalysisIR &analysis,
                               const MorphologicalNotice notice) {
        return std::ranges::contains(analysis.assessment.notice_values(),
                                     notice);
    };

    const auto ausim = test::engine().analyze("ausim");
    const auto exceptional_perfect =
        std::ranges::find_if(ausim.analyses, [](const AnalysisIR &analysis) {
            const auto *verb =
                std::get_if<VerbMorphology>(&analysis.morphology);
            return verb != nullptr && verb->tense == Tense::perfect &&
                   verb->voice == Voice::active;
        });
    ASSERT_NE(exceptional_perfect, ausim.analyses.end());
    EXPECT_TRUE(has_notice(*exceptional_perfect,
                           MorphologicalNotice::source_disagreement));
    EXPECT_TRUE(has_notice(*exceptional_perfect,
                           MorphologicalNotice::manual_review_recommended));

    const auto diffideretur = test::engine().analyze("diffideretur");
    ASSERT_EQ(diffideretur.analyses.size(), 1U);
    EXPECT_TRUE(
        has_notice(diffideretur.analyses.front(),
                   MorphologicalNotice::related_passive_usage_attested));
    EXPECT_TRUE(has_notice(diffideretur.analyses.front(),
                           MorphologicalNotice::source_disagreement));
    EXPECT_TRUE(has_notice(diffideretur.analyses.front(),
                           MorphologicalNotice::manual_review_recommended));
}

TEST(EngineTest, ControlsOrthographyEraWithoutLosingRewriteProvenance) {
    auto classical = AnalysisOptions{};
    classical.orthography = OrthographyMode::classical_only;
    EXPECT_EQ(test::engine().analyze("pretor", classical).status,
              QueryStatus::analyzed);
    EXPECT_EQ(test::engine().analyze("teologia", classical).status,
              QueryStatus::unknown);

    auto disabled = classical;
    disabled.orthography = OrthographyMode::disabled;
    EXPECT_EQ(test::engine().analyze("pretor", disabled).status,
              QueryStatus::unknown);

    const auto medieval = test::engine().analyze("teologia");
    const auto json = Json::parse(analysis_json_v2(test::engine(), medieval));
    const auto &step =
        json.at("analyses").front().at("derivation").at("steps").front();
    EXPECT_EQ(step.at("category"), "medieval");
    EXPECT_EQ(step.at("application").at("observed"), "t");
    EXPECT_EQ(step.at("application").at("replacement"), "th");
    EXPECT_EQ(json.at("analyses").front().at("derivation").at("recognizedForm"),
              "theologia");
}

TEST(EngineTest, IndependentlyControlsMorphologicalMechanisms) {
    struct Fixture final {
        std::string_view surface;
        bool MorphologicalMechanisms::*mechanism;
    };
    constexpr std::array fixtures{
        Fixture{.surface = "archipuella",
                .mechanism = &MorphologicalMechanisms::prefixes},
        Fixture{.surface = "anaticulus",
                .mechanism = &MorphologicalMechanisms::suffixes},
        Fixture{.surface = "ecquidam",
                .mechanism = &MorphologicalMechanisms::tickons},
        Fixture{.surface = "puellaque",
                .mechanism = &MorphologicalMechanisms::tackons},
        Fixture{.surface = "quispiam",
                .mechanism = &MorphologicalMechanisms::packons},
    };
    for (const auto &fixture : fixtures) {
        ASSERT_EQ(test::engine().analyze(fixture.surface).status,
                  QueryStatus::analyzed)
            << fixture.surface;
        auto options = AnalysisOptions{};
        options.mechanisms.*(fixture.mechanism) = false;
        EXPECT_EQ(test::engine().analyze(fixture.surface, options).status,
                  QueryStatus::unknown)
            << fixture.surface;
    }

    auto no_fixes = AnalysisOptions{};
    no_fixes.mechanisms.productive_derivations = false;
    EXPECT_EQ(test::engine().analyze("archipuella", no_fixes).status,
              QueryStatus::unknown);
    EXPECT_EQ(test::engine().analyze("anaticulus", no_fixes).status,
              QueryStatus::unknown);

    auto no_syncope = AnalysisOptions{};
    no_syncope.mechanisms.syncope = false;
    const auto unsyncopated = test::engine().analyze("amasti", no_syncope);
    EXPECT_TRUE(std::ranges::none_of(
        unsyncopated.analyses, [](const AnalysisIR &analysis) {
            return analysis.derivation.rewritten_form.has_value();
        }));

    auto no_compounds = AnalysisOptions{};
    no_compounds.mechanisms.verbal_compounds = false;
    EXPECT_EQ(test::engine().analyze_text("amata est", no_compounds).status,
              QueryStatus::error);
}

TEST(EngineTest, EmitsTypedRemainingMorphologies) {
    const auto contains = []<class T>(const QueryResult &result, const T &) {
        return std::ranges::any_of(
            result.analyses, [](const AnalysisIR &analysis) {
                return std::holds_alternative<T>(analysis.morphology);
            });
    };
    EXPECT_TRUE(contains(test::engine().analyze("duo"), NumeralMorphology{}));
    EXPECT_TRUE(contains(test::engine().analyze("bene"), AdverbMorphology{}));
    EXPECT_TRUE(contains(test::engine().analyze("amo"), VerbMorphology{}));
    EXPECT_TRUE(
        contains(test::engine().analyze("amans"), ParticipleMorphology{}));
    EXPECT_TRUE(contains(test::engine().analyze("amatum"), SupineMorphology{}));
    EXPECT_TRUE(
        contains(test::engine().analyze("cum"), PrepositionMorphology{}));
    EXPECT_TRUE(contains(test::engine().analyze("et"), InvariableMorphology{}));
}

TEST(EngineTest, PreservesParticipleAndSupineInAnalysisOutput) {
    const auto expect_part = [](const std::string_view word,
                                const std::string_view expected) {
        const auto result = test::engine().analyze(word);
        const auto full = Json::parse(analysis_json(test::engine(), result));
        const auto found = std::ranges::find_if(
            full.at("analyses"), [&](const Json &analysis) {
                const auto &part_of_speech =
                    analysis.at("partOfSpeech")
                        .get_ref<const Json::string_t &>();
                return std::string_view{part_of_speech} == expected;
            });
        ASSERT_NE(found, full.at("analyses").end()) << word;
        EXPECT_EQ(found->at("lexeme")
                      .at("partOfSpeech")
                      .get_ref<const Json::string_t &>(),
                  "verb")
            << word;
    };

    expect_part("amans", "participle");
    expect_part("amatum", "supine");
}

TEST(EngineTest, CombinesUniqueAndRegularHomographs) {
    const auto result = test::engine().analyze("eadem");
    ASSERT_EQ(result.status, QueryStatus::analyzed);
    ASSERT_EQ(result.analyses.size(), 4U);
    EXPECT_EQ(std::ranges::count_if(result.analyses,
                                    [](const AnalysisIR &analysis) {
                                        return !analysis.rule.has_value();
                                    }),
              3U);

    const auto full = Json::parse(analysis_json(test::engine(), result));
    EXPECT_EQ(
        std::ranges::count_if(
            full.at("analyses"),
            [](const Json &analysis) {
                return analysis.at("lexeme").at("dictionary") == "unique" &&
                       analysis.at("derivation").at("method") == "unique" &&
                       analysis.at("form").at("stemKey") == nullptr;
            }),
        3U);

    const auto search = Json::parse(search_json(test::engine(), result));
    EXPECT_EQ(std::ranges::count_if(
                  search.at("hits"),
                  [](const Json &hit) { return hit.at("ruleId") == nullptr; }),
              3U);
}

TEST(EngineTest, AppliesEncliticToUniqueAnalysis) {
    const auto result = test::engine().analyze("mavisque");
    ASSERT_EQ(result.status, QueryStatus::analyzed);
    ASSERT_EQ(result.analyses.size(), 1U);
    EXPECT_FALSE(result.analyses.front().rule.has_value());
    EXPECT_EQ(result.analyses.front().derivation.count, 1U);

    const auto full = Json::parse(analysis_json(test::engine(), result));
    const auto &analysis = full.at("analyses").front();
    EXPECT_EQ(analysis.at("derivation").at("method"), "unique");
    EXPECT_EQ(analysis.at("derivation").at("steps").front().at("text"), "que");
}

TEST(EngineTest, DerivesRemainingSuffixTargetClasses) {
    constexpr std::array<std::pair<std::string_view, std::size_t>, 3> fixtures{{
        {"amesco", 3U},
        {"boniter", 1U},
        {"binteni", 2U},
    }};
    for (const auto &[word, count] : fixtures) {
        const auto result = test::engine().analyze(word);
        ASSERT_EQ(result.status, QueryStatus::analyzed) << word;
        ASSERT_EQ(result.analyses.size(), count) << word;
        EXPECT_TRUE(std::ranges::all_of(result.analyses,
                                        [](const AnalysisIR &analysis) {
                                            return analysis.derivation.count >
                                                   0U;
                                        }))
            << word;
    }
}

TEST(EngineTest, ReportsUnknownWord) {
    const auto result = test::engine().analyze("zzzzzz");
    EXPECT_EQ(result.status, QueryStatus::unknown);
    EXPECT_TRUE(result.analyses.empty());
    EXPECT_FALSE(result.two_word_suggestion.has_value());
}

TEST(EngineTest, SuggestsLegacyTwoWordSplitOnlyWhenRequested) {
    const auto disabled = test::engine().analyze("respublica");
    ASSERT_EQ(disabled.status, QueryStatus::unknown);
    EXPECT_FALSE(disabled.two_word_suggestion.has_value());

    constexpr AnalysisOptions legacy{TwoWordsMode::legacy_first_match};
    const auto result = test::engine().analyze("respublica", legacy);
    ASSERT_EQ(result.status, QueryStatus::unknown);
    EXPECT_TRUE(result.analyses.empty());
    ASSERT_TRUE(result.two_word_suggestion.has_value());
    const auto &suggestion = *result.two_word_suggestion;
    EXPECT_EQ(suggestion.logical_split, 3U);
    EXPECT_FALSE(suggestion.both_contain_numeral);
    EXPECT_EQ(suggestion.segments[0].surface.normalized_nfc, "res");
    EXPECT_EQ(suggestion.segments[1].surface.normalized_nfc, "publica");
    EXPECT_FALSE(suggestion.segments[0].analyses.empty());
    EXPECT_FALSE(suggestion.segments[1].analyses.empty());
    EXPECT_TRUE(std::ranges::all_of(
        suggestion.segments, [](const WordSegmentIR &segment) {
            return std::ranges::all_of(
                segment.analyses, [](const AnalysisIR &analysis) {
                    return !analysis.derivation.rewritten_form.has_value();
                });
        }));
    ASSERT_EQ(result.diagnostics.size(), 2U);
    EXPECT_EQ(result.diagnostics.back().code,
              DiagnosticCode::two_words_suggestion);

    const auto full = Json::parse(analysis_json(test::engine(), result));
    ASSERT_EQ(full.at("suggestions").size(), 1U);
    const auto &full_suggestion = full.at("suggestions").front();
    EXPECT_EQ(full_suggestion.at("method"), "two-words");
    EXPECT_EQ(full_suggestion.at("splitAt"), 3U);
    ASSERT_EQ(full_suggestion.at("segments").size(), 2U);
    EXPECT_FALSE(full_suggestion.at("segments").at(0).at("analyses").empty());

    const auto search = Json::parse(search_json(test::engine(), result));
    EXPECT_TRUE(search.at("hits").empty());
    ASSERT_EQ(search.at("suggestions").size(), 1U);
    EXPECT_FALSE(search.at("suggestions")
                     .front()
                     .at("segments")
                     .at(1)
                     .at("hits")
                     .empty());
}

TEST(EngineTest, BoundsAndClassifiesLegacyTwoWordSuggestions) {
    constexpr AnalysisOptions legacy{TwoWordsMode::legacy_first_match};

    const auto first = test::engine().analyze("annam", legacy);
    ASSERT_TRUE(first.two_word_suggestion.has_value());
    EXPECT_EQ(first.two_word_suggestion->logical_split, 2U);
    EXPECT_EQ(first.two_word_suggestion->segments[0].surface.normalized_nfc,
              "an");
    EXPECT_EQ(first.two_word_suggestion->segments[1].surface.normalized_nfc,
              "nam");

    const auto numerals = test::engine().analyze("unustres", legacy);
    ASSERT_TRUE(numerals.two_word_suggestion.has_value());
    EXPECT_TRUE(numerals.two_word_suggestion->both_contain_numeral);

    // A common prefix must not masquerade as the first independent word.
    EXPECT_FALSE(test::engine()
                     .analyze("insed", legacy)
                     .two_word_suggestion.has_value());
    EXPECT_FALSE(test::engine()
                     .analyze("Respublica", legacy)
                     .two_word_suggestion.has_value());

    // A stronger direct/addon analysis always wins over the speculative path.
    const auto direct = test::engine().analyze("paterfamilias", legacy);
    EXPECT_EQ(direct.status, QueryStatus::analyzed);
    EXPECT_FALSE(direct.two_word_suggestion.has_value());
}

TEST(EngineTest, SplitsTwoWordSuggestionAtLogicalUtf8Boundary) {
    constexpr AnalysisOptions legacy{TwoWordsMode::legacy_first_match};
    const auto result = test::engine().analyze("re\xCC\x84spublica", legacy);
    ASSERT_TRUE(result.two_word_suggestion.has_value());
    EXPECT_EQ(result.surface.normalized_nfc, "rēspublica");
    EXPECT_EQ(result.two_word_suggestion->logical_split, 3U);
    EXPECT_EQ(result.two_word_suggestion->segments[0].surface.normalized_nfc,
              "rēs");
}

TEST(EngineTest, PreservesHistoricalFinalTokensAndIssue70Split) {
    const auto line = test::engine().analyze_line("amo o");
    ASSERT_EQ(line.size(), 2U);
    EXPECT_EQ(line.back().surface.normalized_nfc, "o");
    EXPECT_EQ(line.back().status, QueryStatus::analyzed);

    constexpr std::array roman_numerals{
        std::pair{"I", 1U},   std::pair{"V", 5U},   std::pair{"X", 10U},
        std::pair{"L", 50U},  std::pair{"C", 100U}, std::pair{"D", 500U},
        std::pair{"M", 1000U},
    };
    for (const auto &[numeral, expected_value] : roman_numerals) {
        const auto result = test::engine().analyze_line(numeral);
        ASSERT_EQ(result.size(), 1U) << numeral;
        EXPECT_EQ(result.front().status, QueryStatus::analyzed) << numeral;
        ASSERT_EQ(result.front().artificial_analyses.size(), 1U) << numeral;
        const auto *roman =
            std::get_if<RomanNumeralIR>(&result.front().artificial_analyses.front());
        ASSERT_NE(roman, nullptr) << numeral;
        EXPECT_EQ(roman->value, expected_value) << numeral;
        EXPECT_TRUE(roman->well_formed) << numeral;
    }

    const auto direct = test::engine().analyze("bestiasviginti");
    EXPECT_EQ(direct.status, QueryStatus::unknown);

    constexpr AnalysisOptions legacy{TwoWordsMode::legacy_first_match};
    const auto recovered = test::engine().analyze("bestiasviginti", legacy);
    ASSERT_TRUE(recovered.two_word_suggestion.has_value());
    EXPECT_EQ(recovered.two_word_suggestion->segments[0].surface.normalized_nfc,
              "bestias");
    EXPECT_EQ(recovered.two_word_suggestion->segments[1].surface.normalized_nfc,
              "viginti");
}

TEST(EngineTest, CoversReportedDeleoFormsAndCurroWithoutTrailingSpace) {
    struct Fixture final {
        std::string_view surface;
        Person person;
        GrammaticalNumber number;
    };
    constexpr std::array fixtures{
        Fixture{"deleatur", Person::third, GrammaticalNumber::singular},
        Fixture{"deleantur", Person::third, GrammaticalNumber::plural},
        Fixture{"deleamini", Person::second, GrammaticalNumber::plural},
    };
    const auto &database = test::engine().database();
    for (const auto &[surface, person, number] : fixtures) {
        const auto result = test::engine().analyze(surface);
        EXPECT_TRUE(std::ranges::any_of(result.analyses, [&](const AnalysisIR
                                                                 &analysis) {
            const auto *verb =
                std::get_if<VerbMorphology>(&analysis.morphology);
            return verb != nullptr && verb->conjugation == 2U &&
                   verb->tense == Tense::present &&
                   verb->voice == Voice::passive &&
                   verb->mood == Mood::subjunctive && verb->person == person &&
                   verb->number == number &&
                   citation_lemma(database, database.lexeme(analysis.lexeme)) ==
                       "deleo";
        })) << surface;
    }

    const auto curro = test::engine().analyze("curro");
    ASSERT_EQ(curro.status, QueryStatus::analyzed);
    EXPECT_TRUE(
        std::ranges::any_of(curro.analyses, [&](const AnalysisIR &analysis) {
            const auto *verb =
                std::get_if<VerbMorphology>(&analysis.morphology);
            return verb != nullptr && verb->tense == Tense::present &&
                   verb->voice == Voice::active &&
                   verb->mood == Mood::indicative &&
                   verb->person == Person::first &&
                   verb->number == GrammaticalNumber::singular &&
                   citation_lemma(database, database.lexeme(analysis.lexeme)) ==
                       "curro";
        }));
}

TEST(EngineTest, CorrectsHiscoToThirdConjugationPresentActiveInfinitive) {
    const auto result = test::engine().analyze("hiscere");
    ASSERT_EQ(result.status, QueryStatus::analyzed);

    const auto &database = test::engine().database();
    EXPECT_TRUE(std::ranges::any_of(
        result.analyses, [&](const AnalysisIR &analysis) {
            const auto *verb = std::get_if<VerbMorphology>(&analysis.morphology);
            if (verb == nullptr || verb->conjugation != 3U ||
                verb->tense != Tense::present || verb->voice != Voice::active ||
                verb->mood != Mood::infinitive) {
                return false;
            }
            const auto &lexeme = database.lexeme(analysis.lexeme);
            return citation_lemma(database, lexeme) == "hisco" &&
                   dictionary_form(database, lexeme).find("hiscere") !=
                       std::string::npos;
        }))
        << "hiscere must retain the corrected hisco, hiscere analysis";
}

TEST(EngineTest, PreservesSancteHomographsAndVidesneEnclitic) {
    const auto sancte = test::engine().analyze("sancte");
    const auto &database = test::engine().database();
    EXPECT_TRUE(
        std::ranges::any_of(sancte.analyses, [&](const AnalysisIR &analysis) {
            const auto *const adjective =
                std::get_if<AdjectiveMorphology>(&analysis.morphology);
            if (adjective == nullptr ||
                adjective->grammatical_case != GrammaticalCase::vocative ||
                citation_lemma(database, database.lexeme(analysis.lexeme)) !=
                    "sanctus") {
                return false;
            }
            return adjective->number == GrammaticalNumber::singular &&
                   adjective->gender == Gender::masculine;
        }));
    EXPECT_TRUE(
        std::ranges::any_of(sancte.analyses, [&](const AnalysisIR &analysis) {
            const auto *const adverb =
                std::get_if<AdverbMorphology>(&analysis.morphology);
            if (adverb == nullptr ||
                citation_lemma(database, database.lexeme(analysis.lexeme)) !=
                    "sanctus") {
                return false;
            }
            return std::ranges::any_of(
                analysis.derivation.steps(), [&](const AddonId addon) {
                    return database.addon_kind(addon) == AddonKind::suffix &&
                           database.suffix_string(database.suffix(addon).fix) ==
                               "e";
                });
        }));
    /* Keep the weaker category checks below as diagnostics for future
       datasets that may add a second vocative or adverbial homograph. */
    EXPECT_TRUE(
        std::ranges::any_of(sancte.analyses, [](const AnalysisIR &analysis) {
            return std::holds_alternative<AdverbMorphology>(
                analysis.morphology);
        }));
    EXPECT_TRUE(
        std::ranges::any_of(sancte.analyses, [](const AnalysisIR &analysis) {
            const auto *adjective =
                std::get_if<AdjectiveMorphology>(&analysis.morphology);
            return adjective != nullptr &&
                   adjective->grammatical_case == GrammaticalCase::vocative;
        }));

    const auto videsne = test::engine().analyze("videsne");
    ASSERT_EQ(videsne.status, QueryStatus::analyzed);
    ASSERT_FALSE(videsne.analyses.empty());
    EXPECT_TRUE(
        std::ranges::any_of(videsne.analyses, [&](const AnalysisIR &analysis) {
            const auto *verb =
                std::get_if<VerbMorphology>(&analysis.morphology);
            if (verb == nullptr || verb->person != Person::second ||
                verb->number != GrammaticalNumber::singular) {
                return false;
            }
            if (citation_lemma(database, database.lexeme(analysis.lexeme)) !=
                "video") {
                return false;
            }
            return std::ranges::any_of(
                analysis.derivation.steps(), [&](const AddonId addon) {
                    return database.addon_kind(addon) == AddonKind::tackon &&
                           database.tackon_string(database.tackon(addon).fix) ==
                               "ne";
                });
        }));
}

TEST(EngineTest, EmitsRomanNumeralsWithoutSyntheticLexemeIds) {
    const auto result = test::engine().analyze("IV");
    ASSERT_EQ(result.status, QueryStatus::analyzed);
    ASSERT_EQ(result.artificial_analyses.size(), 1U);
    const auto *roman =
        std::get_if<RomanNumeralIR>(&result.artificial_analyses.front());
    ASSERT_NE(roman, nullptr);
    EXPECT_EQ(roman->value, 4U);
    EXPECT_TRUE(roman->well_formed);

    const auto full = Json::parse(analysis_json(test::engine(), result));
    ASSERT_EQ(full.at("analyses").size(), 1U);
    EXPECT_EQ(full.at("analyses").front().at("lexeme").at("entryId"), nullptr);
    EXPECT_EQ(full.at("analyses").front().at("derivation").at("method"),
              "roman-numeral");

    const auto search = Json::parse(search_json(test::engine(), result));
    ASSERT_EQ(search.at("hits").size(), 1U);
    EXPECT_EQ(search.at("hits").front().at("lexemeId"), nullptr);
    EXPECT_EQ(search.at("hits").front().at("artificial").at("value"), 4);
}

TEST(EngineTest, KeepsRomanHomographsAndMarksIllFormedFallbacks) {
    const auto homograph = test::engine().analyze("mi");
    EXPECT_FALSE(homograph.analyses.empty());
    ASSERT_EQ(homograph.artificial_analyses.size(), 1U);
    EXPECT_EQ(
        std::get<RomanNumeralIR>(homograph.artificial_analyses.front()).value,
        1001U);

    const auto fallback = test::engine().analyze("IIV");
    ASSERT_EQ(fallback.status, QueryStatus::analyzed);
    ASSERT_EQ(fallback.artificial_analyses.size(), 1U);
    const auto roman =
        std::get<RomanNumeralIR>(fallback.artificial_analyses.front());
    EXPECT_EQ(roman.value, 3U);
    EXPECT_FALSE(roman.well_formed);

    const auto enclitic = test::engine().analyze("ivque");
    ASSERT_EQ(enclitic.artificial_analyses.size(), 1U);
    const auto &with_tackon =
        std::get<RomanNumeralIR>(enclitic.artificial_analyses.front());
    EXPECT_EQ(with_tackon.value, 4U);
    EXPECT_FALSE(with_tackon.well_formed);
    EXPECT_EQ(with_tackon.derivation.count, 1U);

    const auto full = Json::parse(analysis_json(test::engine(), enclitic));
    const auto &steps =
        full.at("analyses").front().at("derivation").at("steps");
    ASSERT_EQ(steps.size(), 2U);
    EXPECT_EQ(steps.front().at("type"), "tackon");
    EXPECT_EQ(steps.back().at("type"), "roman-numeral");
}

TEST(EngineTest, FlagsPeriodAbbreviationConflictWithoutDroppingRomanReading) {
    const auto result = test::engine().analyze_text("C.");
    ASSERT_EQ(result.status, QueryStatus::analyzed);
    ASSERT_FALSE(result.analyses.empty());
    ASSERT_EQ(result.artificial_analyses.size(), 1U);

    const auto &roman =
        std::get<RomanNumeralIR>(result.artificial_analyses.front());
    EXPECT_EQ(roman.value, 100U);
    EXPECT_TRUE(
        std::ranges::contains(roman.assessment.notice_values(),
                              MorphologicalNotice::source_disagreement));

    const auto full = Json::parse(analysis_json_v2(test::engine(), result));
    const auto full_roman =
        std::ranges::find_if(full.at("analyses"), [](const Json &analysis) {
            return analysis.at("derivation").at("method") == "roman-numeral";
        });
    ASSERT_NE(full_roman, full.at("analyses").end());
    const auto &full_assessment = full_roman->at("assessment");
    EXPECT_EQ(full_assessment.at("notices").front().at("code"),
              "source-disagreement");

    const auto search = Json::parse(search_json_v2(test::engine(), result));
    const auto search_roman =
        std::ranges::find_if(search.at("hits"), [](const Json &hit) {
            return hit.contains("artificial");
        });
    ASSERT_NE(search_roman, search.at("hits").end());
    const auto &search_assessment = search_roman->at("assessment");
    EXPECT_EQ(search_assessment.at("notices").front().at("code"),
              "source-disagreement");

    const auto bare = test::engine().analyze("C");
    ASSERT_EQ(bare.artificial_analyses.size(), 1U);
    const auto &bare_roman =
        std::get<RomanNumeralIR>(bare.artificial_analyses.front());
    EXPECT_FALSE(
        std::ranges::contains(bare_roman.assessment.notice_values(),
                              MorphologicalNotice::source_disagreement));
}

TEST(EngineTest, AppliesDataDrivenPerfectSyncopeByPriority) {
    struct Fixture final {
        std::string_view word;
        std::string_view rule_name;
        std::uint8_t priority;
    };
    constexpr std::array fixtures{
        Fixture{"audiisti", "perfect-ivi-uncontracted", 0U},
        Fixture{"amasti", "perfect-v-contraction", 1U},
        Fixture{"audisti", "perfect-v-contraction", 1U},
        Fixture{"amarunt", "perfect-v-before-r", 2U},
        Fixture{"audierunt", "perfect-ier", 3U},
        Fixture{"scripsti", "perfect-is-after-s-x", 4U},
    };
    for (const auto &[word, rule_name, priority] : fixtures) {
        const auto result = test::engine().analyze(word);
        ASSERT_EQ(result.status, QueryStatus::analyzed) << word;
        ASSERT_EQ(result.analyses.size(), 1U) << word;
        const auto &rewritten =
            result.analyses.front().derivation.rewritten_form;
        ASSERT_TRUE(rewritten.has_value()) << word;
        EXPECT_EQ(test::engine()
                      .database()
                      .rewrite(rewritten->rules.front())
                      .priority,
                  priority)
            << word;

        const auto full = Json::parse(analysis_json(test::engine(), result));
        const auto &derivation = full.at("analyses").front().at("derivation");
        EXPECT_EQ(derivation.at("method"), "syncope") << word;
        ASSERT_EQ(derivation.at("steps").size(), 1U) << word;
        const auto &serialized_rule = derivation.at("steps")
                                          .front()
                                          .at("rule")
                                          .get_ref<const Json::string_t &>();
        EXPECT_EQ(std::string_view{serialized_rule}, rule_name) << word;

        const auto search = Json::parse(search_json(test::engine(), result));
        ASSERT_EQ(search.at("hits").size(), 1U) << word;
        EXPECT_TRUE(search.at("hits").front().contains("rewriteIds")) << word;
    }
}

TEST(EngineTest, RejectsProductiveAddonsInsidePerfectSyncope) {
    constexpr std::array<std::string_view, 5> ordinary_forms{
        "adfare", "colere", "desine", "dicere", "ducere",
    };
    for (const std::string_view word : ordinary_forms) {
        const auto result = test::engine().analyze(word);
        ASSERT_EQ(result.status, QueryStatus::analyzed) << word;
        EXPECT_TRUE(std::ranges::none_of(result.analyses, [](const AnalysisIR
                                                                 &analysis) {
            return analysis.derivation.rewritten_form.has_value();
        })) << word;
    }

    // WHY: after rejecting the earlier de- + viso guess, the scheduler must
    // continue to the direct desero perfect-system reconstruction.
    constexpr std::array<std::string_view, 2> contracted_forms{"deseris",
                                                               "deserit"};
    for (const std::string_view word : contracted_forms) {
        const auto result = test::engine().analyze(word);
        const auto syncopated = std::ranges::count_if(
            result.analyses, [](const AnalysisIR &analysis) {
                return analysis.derivation.rewritten_form.has_value();
            });
        EXPECT_GT(syncopated, 0U) << word;
        EXPECT_TRUE(std::ranges::all_of(result.analyses, [](const AnalysisIR
                                                                &analysis) {
            return !analysis.derivation.rewritten_form.has_value() ||
                   analysis.derivation.count == 0U;
        })) << word;
    }
}

TEST(EngineTest, AllowsPerfectSyncopeInsideOneOuterEnclitic) {
    const auto result = test::engine().analyze("implessemque");
    ASSERT_EQ(result.status, QueryStatus::analyzed);
    ASSERT_EQ(result.analyses.size(), 1U);
    const auto &analysis = result.analyses.front();
    ASSERT_TRUE(analysis.derivation.rewritten_form.has_value());
    ASSERT_EQ(analysis.derivation.count, 1U);
    EXPECT_EQ(test::engine().database().addon_kind(
                  analysis.derivation.steps().front()),
              AddonKind::tackon);
}

TEST(EngineTest, DoesNotRewriteOrdinaryOrUnvalidatedForms) {
    for (const auto *const word : {"amavisti", "servus", "zzzzaszz"}) {
        const auto result = test::engine().analyze(word);
        EXPECT_TRUE(std::ranges::none_of(result.analyses, [](const AnalysisIR
                                                                 &analysis) {
            return analysis.derivation.rewritten_form.has_value();
        })) << word;
    }
}

TEST(EngineTest, AppliesDataDrivenOrthographicFamilies) {
    constexpr std::array<std::pair<std::string_view, std::string_view>, 6>
        fixtures{{
            {"pretor", "orth-initial-pre-prae"},
            {"philosofus", "orth-internal-f-ph"},
            {"teologia", "orth-internal-t-th"},
            {"literatura", "orth-medieval-double-consonant"},
            {"obpono", "orth-slur-ob"},
            {"propris", "orth-adjective-terminal-iis"},
        }};
    for (const auto &[word, rule_name] : fixtures) {
        const auto result = test::engine().analyze(word);
        ASSERT_EQ(result.status, QueryStatus::analyzed) << word;
        ASSERT_FALSE(result.analyses.empty()) << word;
        EXPECT_TRUE(std::ranges::all_of(result.analyses, [](const AnalysisIR
                                                                &analysis) {
            return analysis.derivation.rewritten_form.has_value();
        })) << word;

        const auto full = Json::parse(analysis_json(test::engine(), result));
        for (const auto &analysis : full.at("analyses")) {
            EXPECT_EQ(analysis.at("derivation").at("method"), "orthographic")
                << word;
            const auto &serialized_rule =
                analysis.at("derivation")
                    .at("steps")
                    .front()
                    .at("rule")
                    .get_ref<const Json::string_t &>();
            EXPECT_EQ(std::string_view{serialized_rule}, rule_name) << word;
        }
        const auto search = Json::parse(search_json(test::engine(), result));
        EXPECT_TRUE(std::ranges::all_of(search.at("hits"), [](const Json &hit) {
            return hit.at("rewriteIds").size() == 1U;
        })) << word;
    }
}

TEST(EngineTest, BoundsOrthographyThenSyncopeToTwoTypedSteps) {
    const auto result = test::engine().analyze("ahmasti");
    ASSERT_EQ(result.status, QueryStatus::analyzed);
    ASSERT_EQ(result.analyses.size(), 1U);
    const auto &rewritten = *result.analyses.front().derivation.rewritten_form;
    EXPECT_EQ(rewritten.count, 2U);

    const auto full = Json::parse(analysis_json(test::engine(), result));
    const auto &derivation = full.at("analyses").front().at("derivation");
    EXPECT_EQ(derivation.at("method"), "orthographic");
    ASSERT_EQ(derivation.at("steps").size(), 2U);
    EXPECT_EQ(derivation.at("steps").at(0).at("type"), "orthographic");
    EXPECT_EQ(derivation.at("steps").at(1).at("type"), "syncope");

    const auto search = Json::parse(search_json(test::engine(), result));
    EXPECT_EQ(search.at("hits").front().at("rewriteIds").size(), 2U);
}

TEST(EngineTest, OrthographicRewritePreservesUnaffectedMacrons) {
    const auto result = test::engine().analyze("philo\xCC\x84sofus");
    ASSERT_EQ(result.status, QueryStatus::analyzed);
    const auto full = Json::parse(analysis_json(test::engine(), result));
    EXPECT_EQ(full.at("query").at("normalized"), "philōsofus");
    EXPECT_TRUE(std::ranges::all_of(full.at("analyses"), [](const Json &item) {
        return item.at("form").at("stem") == "philōsoph";
    }));
}

TEST(EngineTest, PreservesTackonBeforeOrthographicRecovery) {
    const auto result = test::engine().analyze("pretoribusque");
    ASSERT_EQ(result.status, QueryStatus::analyzed);
    ASSERT_EQ(result.analyses.size(), 3U);

    const auto full = Json::parse(analysis_json(test::engine(), result));
    for (const auto &analysis : full.at("analyses")) {
        const auto &steps = analysis.at("derivation").at("steps");
        ASSERT_EQ(steps.size(), 2U);
        EXPECT_EQ(steps.at(0).at("type"), "tackon");
        EXPECT_EQ(steps.at(1).at("type"), "orthographic");
    }

    const auto search = Json::parse(search_json(test::engine(), result));
    for (const auto &hit : search.at("hits")) {
        EXPECT_EQ(hit.at("addonIds").size(), 1U);
        EXPECT_EQ(hit.at("rewriteIds").size(), 1U);
    }
}

TEST(EngineTest, DoesNotCombineOrthographyWithProductiveAddons) {
    constexpr std::array<std::string_view, 12> forms{
        "aulide",   "barcaei",   "cresia",     "cynthi",
        "dardanus", "dictaeos",  "erebi",      "hesperidum",
        "lenaeum",  "phoenissa", "phoenissam", "xanthique",
    };
    for (const std::string_view word : forms) {
        const auto result = test::engine().analyze(word);
        EXPECT_EQ(result.status, QueryStatus::unknown) << word;
        EXPECT_TRUE(result.analyses.empty()) << word;
    }

    // Uppercase Roman records in ADDONS.LAT are notation, not productive
    // lowercase prefixes over arbitrary nouns.
    EXPECT_EQ(test::engine().analyze("xservus").status, QueryStatus::unknown);
}

TEST(EngineTest, AnalyzesBoundedCompoundsWithSum) {
    struct Fixture final {
        std::string_view text;
        CompoundKind kind;
        Tense tense;
        Voice voice;
        Mood mood;
        Person person;
        GrammaticalNumber number;
    };
    constexpr std::array fixtures{
        Fixture{.text = "amata est",
                .kind = CompoundKind::finite_sum,
                .tense = Tense::perfect,
                .voice = Voice::passive,
                .mood = Mood::indicative,
                .person = Person::third,
                .number = GrammaticalNumber::singular},
        Fixture{.text = "amati sunt",
                .kind = CompoundKind::finite_sum,
                .tense = Tense::perfect,
                .voice = Voice::passive,
                .mood = Mood::indicative,
                .person = Person::third,
                .number = GrammaticalNumber::plural},
        Fixture{.text = "amata fuerit",
                .kind = CompoundKind::finite_sum,
                .tense = Tense::unknown,
                .voice = Voice::passive,
                .mood = Mood::indicative,
                .person = Person::third,
                .number = GrammaticalNumber::singular},
        Fixture{.text = "amaturus est",
                .kind = CompoundKind::finite_sum,
                .tense = Tense::present,
                .voice = Voice::passive,
                .mood = Mood::indicative,
                .person = Person::third,
                .number = GrammaticalNumber::singular},
        Fixture{.text = "amatus esse",
                .kind = CompoundKind::esse,
                .tense = Tense::perfect,
                .voice = Voice::passive,
                .mood = Mood::infinitive,
                .person = Person::unknown,
                .number = GrammaticalNumber::unknown},
        Fixture{.text = "amaturus fuisse",
                .kind = CompoundKind::fuisse,
                .tense = Tense::perfect,
                .voice = Voice::active,
                .mood = Mood::infinitive,
                .person = Person::unknown,
                .number = GrammaticalNumber::unknown},
        Fixture{.text = "amatum iri",
                .kind = CompoundKind::iri,
                .tense = Tense::future,
                .voice = Voice::passive,
                .mood = Mood::infinitive,
                .person = Person::unknown,
                .number = GrammaticalNumber::unknown},
    };

    for (const auto &fixture : fixtures) {
        const auto result = test::engine().analyze_text(fixture.text);
        ASSERT_EQ(result.status, QueryStatus::analyzed) << fixture.text;
        ASSERT_FALSE(result.analyses.empty()) << fixture.text;
        ASSERT_EQ(result.compound_analyses.size(), 1U) << fixture.text;
        const auto &compound = result.compound_analyses.front();
        EXPECT_EQ(compound.kind, fixture.kind) << fixture.text;
        EXPECT_EQ(compound.morphology.tense, fixture.tense) << fixture.text;
        EXPECT_EQ(compound.morphology.voice, fixture.voice) << fixture.text;
        EXPECT_EQ(compound.morphology.mood, fixture.mood) << fixture.text;
        EXPECT_EQ(compound.morphology.person, fixture.person) << fixture.text;
        EXPECT_EQ(compound.morphology.number, fixture.number) << fixture.text;

        const auto full = Json::parse(analysis_json(test::engine(), result));
        ASSERT_EQ(full.at("analyses").size(),
                  result.analyses.size() + result.compound_analyses.size())
            << fixture.text;
        const auto &normalized =
            full.at("query").at("normalized").get_ref<const Json::string_t &>();
        EXPECT_EQ(std::string_view{normalized}, fixture.text);
        const Json *compound_projection = nullptr;
        for (const auto &analysis : full.at("analyses")) {
            if (analysis.at("derivation").at("method") == "compound") {
                compound_projection = &analysis;
                break;
            }
        }
        ASSERT_NE(compound_projection, nullptr) << fixture.text;
        const auto &derivation = compound_projection->at("derivation");
        EXPECT_EQ(derivation.at("method"), "compound") << fixture.text;
        ASSERT_EQ(derivation.at("steps").size(), 1U) << fixture.text;
        EXPECT_EQ(derivation.at("steps").front().at("type"), "compound");

        const auto search = Json::parse(search_json(test::engine(), result));
        ASSERT_EQ(search.at("hits").size(),
                  result.analyses.size() + result.compound_analyses.size())
            << fixture.text;
        EXPECT_EQ(std::ranges::count_if(
                      search.at("hits"),
                      [](const Json &hit) { return hit.contains("compound"); }),
                  1U);
    }
}

TEST(EngineTest, FiniteCompoundsSelectNominativeParticiplesMatchingNumber) {
    struct Fixture final {
        std::string_view text;
        GrammaticalNumber number;
    };
    constexpr std::array fixtures{
        Fixture{.text = "amata est", .number = GrammaticalNumber::singular},
        Fixture{.text = "amata sunt", .number = GrammaticalNumber::plural},
        Fixture{.text = "amati sunt", .number = GrammaticalNumber::plural},
    };

    for (const auto &fixture : fixtures) {
        const auto result = test::engine().analyze_text(fixture.text);
        ASSERT_EQ(result.status, QueryStatus::analyzed) << fixture.text;
        ASSERT_EQ(result.compound_analyses.size(), 1U) << fixture.text;
        const auto &compound = result.compound_analyses.front();
        ASSERT_TRUE(compound.source_rule.has_value()) << fixture.text;
        const auto &source =
            test::engine().database().rule(*compound.source_rule);
        EXPECT_EQ(source.part_of_speech, PartOfSpeech::participle)
            << fixture.text;
        EXPECT_EQ(source.grammatical_case, GrammaticalCase::nominative)
            << fixture.text;
        EXPECT_EQ(source.number, fixture.number) << fixture.text;
    }
}

TEST(EngineTest, AnalyzeLinePreservesTokensWhenFiniteAgreementRejectsCompound) {
    const auto isolated_participle = test::engine().analyze("amatam");
    const auto isolated_auxiliary = test::engine().analyze("est");
    const auto results = test::engine().analyze_line("amatam est");

    ASSERT_EQ(results.size(), 2U);
    EXPECT_TRUE(results[0].compound_analyses.empty());
    EXPECT_TRUE(results[1].compound_analyses.empty());
    EXPECT_EQ(semantic_signatures(results[0].analyses),
              semantic_signatures(isolated_participle.analyses));
    EXPECT_EQ(semantic_signatures(results[1].analyses),
              semantic_signatures(isolated_auxiliary.analyses));
}

TEST(EngineTest, CompoundAnalysisPreservesIndependentFirstTokenAnalyses) {
    struct Fixture final {
        std::string_view first;
        std::string_view phrase;
    };
    constexpr std::array fixtures{
        Fixture{.first = "amata", .phrase = "amata est"},
        Fixture{.first = "amatam", .phrase = "amatam esse"},
        Fixture{.first = "amatum", .phrase = "amatum iri"},
    };

    for (const auto &fixture : fixtures) {
        const auto isolated = test::engine().analyze(fixture.first);
        const auto compound = test::engine().analyze_text(fixture.phrase);
        ASSERT_EQ(compound.status, QueryStatus::analyzed) << fixture.phrase;
        ASSERT_FALSE(compound.compound_analyses.empty()) << fixture.phrase;
        EXPECT_EQ(compound.analyses.size(), isolated.analyses.size())
            << fixture.phrase;
        if (compound.analyses.size() != isolated.analyses.size()) {
            continue;
        }
        EXPECT_EQ(semantic_signatures(compound.analyses),
                  semantic_signatures(isolated.analyses))
            << fixture.phrase;
    }
}

TEST(EngineTest, CompoundAnalysisPreservesEveryIndependentToken) {
    struct Fixture final {
        std::string_view first;
        std::string_view second;
        std::string_view phrase;
    };
    constexpr std::array fixtures{
        Fixture{.first = "amata", .second = "est", .phrase = "amata est"},
        Fixture{.first = "amatam", .second = "esse", .phrase = "amatam esse"},
        Fixture{.first = "amatum", .second = "iri", .phrase = "amatum iri"},
    };

    for (const auto &fixture : fixtures) {
        const auto isolated_first = test::engine().analyze(fixture.first);
        const auto isolated_second = test::engine().analyze(fixture.second);
        const auto result = test::engine().analyze_text(fixture.phrase);

        ASSERT_EQ(result.status, QueryStatus::analyzed) << fixture.phrase;
        ASSERT_EQ(result.independent_tokens.size(), 2U) << fixture.phrase;
        EXPECT_EQ(result.independent_tokens[0].surface.normalized_nfc,
                  fixture.first);
        EXPECT_EQ(result.independent_tokens[1].surface.normalized_nfc,
                  fixture.second);
        EXPECT_EQ(semantic_signatures(result.independent_tokens[0].analyses),
                  semantic_signatures(isolated_first.analyses))
            << fixture.phrase;
        EXPECT_EQ(semantic_signatures(result.independent_tokens[1].analyses),
                  semantic_signatures(isolated_second.analyses))
            << fixture.phrase;
        EXPECT_EQ(result.total_analyses(),
                  result.analyses.size() + result.compound_analyses.size() +
                      result.artificial_analyses.size() +
                      isolated_first.total_analyses() +
                      isolated_second.total_analyses())
            << fixture.phrase;

        const auto full = Json::parse(analysis_json_v2(test::engine(), result));
        ASSERT_TRUE(full.contains("tokens")) << fixture.phrase;
        ASSERT_EQ(full.at("tokens").size(), 2U) << fixture.phrase;
        const auto &first_normalized = full.at("tokens")[0]
                                           .at("query")
                                           .at("normalized")
                                           .get_ref<const Json::string_t &>();
        const auto &second_normalized = full.at("tokens")[1]
                                            .at("query")
                                            .at("normalized")
                                            .get_ref<const Json::string_t &>();
        EXPECT_EQ(std::string_view{first_normalized}, fixture.first);
        EXPECT_EQ(std::string_view{second_normalized}, fixture.second);
        EXPECT_EQ(full.at("tokens")[0].at("analyses").size(),
                  isolated_first.analyses.size());
        EXPECT_EQ(full.at("tokens")[1].at("analyses").size(),
                  isolated_second.analyses.size());

        const auto search = Json::parse(search_json_v2(test::engine(), result));
        ASSERT_TRUE(search.contains("tokens")) << fixture.phrase;
        ASSERT_EQ(search.at("tokens").size(), 2U) << fixture.phrase;
        EXPECT_EQ(search.at("tokens")[0].at("hits").size(),
                  isolated_first.analyses.size());
        EXPECT_EQ(search.at("tokens")[1].at("hits").size(),
                  isolated_second.analyses.size());
    }
}

TEST(EngineTest, PreservesCompoundSourceRulesForHomographicParticiples) {
    // The Ada presentation collapses these to one synthetic compound.  The
    // native IR instead retains the four distinct inflection rules that can
    // underlie the homographic -um surface.  A future aggregated hypothesis
    // must preserve all four supports rather than choose one arbitrarily.
    constexpr std::array<std::string_view, 4> phrases{
        "amatum esse", "captum esse", "amaturum esse", "amaturum fuisse"};
    for (const auto phrase : phrases) {
        const auto result = test::engine().analyze_text(phrase);
        ASSERT_EQ(result.status, QueryStatus::analyzed) << phrase;
        ASSERT_EQ(result.compound_analyses.size(), 4U) << phrase;

        std::vector<RuleId> source_rules;
        for (const auto &compound : result.compound_analyses) {
            ASSERT_TRUE(compound.source_rule.has_value()) << phrase;
            source_rules.push_back(*compound.source_rule);
        }
        std::ranges::sort(source_rules);
        EXPECT_EQ(std::ranges::adjacent_find(source_rules), source_rules.end())
            << phrase;
    }
}

TEST(EngineTest, PreservesAuxiliaryDerivationInCompoundAnalysis) {
    const auto result = test::engine().analyze_text("amata estque");
    ASSERT_EQ(result.status, QueryStatus::analyzed);
    ASSERT_EQ(result.compound_analyses.size(), 1U);
    const auto &compound = result.compound_analyses.front();
    EXPECT_EQ(compound.auxiliary, "estque");
    ASSERT_EQ(compound.auxiliary_derivation.steps().size(), 1U);
    const auto addon = compound.auxiliary_derivation.steps().front();
    EXPECT_EQ(test::engine().database().addon_kind(addon), AddonKind::tackon);
    EXPECT_EQ(test::engine().database().tackon_string(
                  test::engine().database().tackon(addon).fix),
              "que");
}

TEST(EngineTest, RejectsGeneralPhraseParsingOutsideCompoundGrammar) {
    for (const auto *const text : {"amata amat", "puella amat servum"}) {
        const auto result = test::engine().analyze_text(text);
        EXPECT_EQ(result.status, QueryStatus::error) << text;
        EXPECT_TRUE(result.analyses.empty()) << text;
        EXPECT_TRUE(result.compound_analyses.empty()) << text;
        ASSERT_EQ(result.diagnostics.size(), 1U) << text;
    }

    const auto padded = test::engine().analyze_text("  amata\t");
    const auto full = Json::parse(analysis_json(test::engine(), padded));
    EXPECT_EQ(full.at("query").at("text"), "  amata\t");
    EXPECT_EQ(full.at("query").at("normalized"), "amata");
}

TEST(EngineTest, AnalyzeLineReusesFailedLookaheadAsAnIndependentWord) {
    const auto results = test::engine().analyze_line("amata\t amare");
    ASSERT_EQ(results.size(), 2U);
    EXPECT_EQ(results[0].status, QueryStatus::analyzed);
    EXPECT_EQ(results[0].surface.normalized_nfc, "amata");
    EXPECT_FALSE(results[0].analyses.empty());
    EXPECT_TRUE(results[0].compound_analyses.empty());
    EXPECT_EQ(results[1].status, QueryStatus::analyzed);
    EXPECT_EQ(results[1].surface.normalized_nfc, "amare");
    EXPECT_FALSE(results[1].analyses.empty());
}

TEST(EngineTest, AnalyzeLineConsumesOnlyRecognizedCompounds) {
    constexpr std::string_view input = "amo\tamatus\xC2\xA0"
                                       "sum\xE2\x80\x83"
                                       "amare";
    const auto results = test::engine().analyze_line(input);
    ASSERT_EQ(results.size(), 3U);
    EXPECT_EQ(results[0].surface.normalized_nfc, "amo");
    EXPECT_EQ(results[2].surface.normalized_nfc, "amare");

    const auto &compound = results[1];
    ASSERT_TRUE(compound.multi_token_query.has_value());
    EXPECT_EQ(compound.multi_token_query->original_utf8, "amatus\xC2\xA0"
                                                         "sum");
    EXPECT_EQ(compound.multi_token_query->normalized_nfc, "amatus sum");
    EXPECT_EQ(compound.status, QueryStatus::analyzed);
    EXPECT_FALSE(compound.compound_analyses.empty());
}

TEST(EngineTest, AnalyzeLineSplitsAsciiAndUnicodePunctuation) {
    const auto results =
        test::engine().analyze_line("“amo”, 'amare'; (amatus.sum)!");
    ASSERT_EQ(results.size(), 4U);
    EXPECT_EQ(results[0].surface.normalized_nfc, "amo");
    EXPECT_EQ(results[1].surface.normalized_nfc, "amare");
    EXPECT_EQ(results[2].surface.normalized_nfc, "amatus");
    EXPECT_EQ(results[3].surface.normalized_nfc, "sum");
    EXPECT_TRUE(results[2].compound_analyses.empty());

    const auto quoted = test::engine().analyze_line("“amatus sum”");
    ASSERT_EQ(quoted.size(), 1U);
    const auto &compound = quoted.front();
    ASSERT_TRUE(compound.multi_token_query.has_value());
    EXPECT_EQ(compound.multi_token_query->normalized_nfc, "amatus sum");
    EXPECT_FALSE(compound.compound_analyses.empty());

    const auto punctuated = test::engine().analyze_text("amatus, sum");
    EXPECT_EQ(punctuated.status, QueryStatus::error);
    ASSERT_EQ(punctuated.diagnostics.size(), 1U);
    EXPECT_EQ(punctuated.diagnostics.front().code,
              DiagnosticCode::unsupported_multi_token);

    const auto single = test::engine().analyze_line("“amo,”");
    ASSERT_EQ(single.size(), 1U);
    EXPECT_EQ(single.front().surface.original_utf8, "amo");
    EXPECT_FALSE(single.front().multi_token_query.has_value());
}

TEST(EngineTest, AnalyzeLinePreservesUtf8NormalizationAndErrorsPerToken) {
    const auto marked =
        test::engine().analyze_line("puella\xCC\x84\vros\xC4\x83");
    ASSERT_EQ(marked.size(), 2U);
    EXPECT_EQ(marked[0].surface.normalized_nfc, "puellā");
    EXPECT_EQ(marked[1].surface.normalized_nfc, "rosă");
    EXPECT_FALSE(marked[0].analyses.empty());
    EXPECT_FALSE(marked[1].analyses.empty());

    std::string malformed{"amo "};
    malformed.append("\xC3\x28", 2U);
    malformed.append("\tamare");
    const auto results = test::engine().analyze_line(malformed);
    ASSERT_EQ(results.size(), 3U);
    EXPECT_EQ(results[0].status, QueryStatus::analyzed);
    EXPECT_EQ(results[1].status, QueryStatus::error);
    ASSERT_FALSE(results[1].diagnostics.empty());
    EXPECT_EQ(results[1].diagnostics.front().code,
              DiagnosticCode::invalid_utf8);
    EXPECT_EQ(results[2].status, QueryStatus::analyzed);
}

TEST(EngineTest, AnalyzeLineAppliesLegacyTwoWordsRecoveryPerToken) {
    constexpr AnalysisOptions legacy{TwoWordsMode::legacy_first_match};
    const auto results = test::engine().analyze_line("respublica amo", legacy);
    ASSERT_EQ(results.size(), 2U);
    EXPECT_EQ(results[0].status, QueryStatus::unknown);
    EXPECT_TRUE(results[0].two_word_suggestion.has_value());
    EXPECT_EQ(results[1].status, QueryStatus::analyzed);
}

TEST(EngineTest, QuantityPartitionsPuellaInflectionsExactly) {
    constexpr std::array expected{
        noun_expectation(32'257U, 1U, 1U, GrammaticalCase::nominative,
                         GrammaticalNumber::singular, Gender::feminine, 1U),
        noun_expectation(32'257U, 1U, 1U, GrammaticalCase::vocative,
                         GrammaticalNumber::singular, Gender::feminine, 1U),
        noun_expectation(32'257U, 1U, 1U, GrammaticalCase::ablative,
                         GrammaticalNumber::singular, Gender::feminine, 2U),
    };

    expect_nominal_analyses("puella", expected, QuantityMatch::unspecified,
                            "puell", "a");
    expect_nominal_analyses("puellă", std::span{expected}.first<2U>(),
                            QuantityMatch::exact, "puell", "ă");
    expect_nominal_analyses("puellā", std::span{expected}.last<1U>(),
                            QuantityMatch::exact, "puell", "ā");
}

TEST(EngineTest, QuantityPartitionsMalumLexemesExactly) {
    // Keeping the long-a group before the short-a group in this fixture lets
    // the marked cases reuse spans of the unmarked semantic set.  The helper
    // sorts signatures and deliberately does not constrain presentation order.
    constexpr std::array expected{
        noun_expectation(26'263U, 1U, 1U, GrammaticalCase::genitive,
                         GrammaticalNumber::plural, Gender::feminine, 2U),
        noun_expectation(26'264U, 2U, 1U, GrammaticalCase::genitive,
                         GrammaticalNumber::plural, Gender::masculine, 2U),
        noun_expectation(26'264U, 2U, 1U, GrammaticalCase::accusative,
                         GrammaticalNumber::singular, Gender::masculine, 2U),
        noun_expectation(26'265U, 2U, 1U, GrammaticalCase::genitive,
                         GrammaticalNumber::plural, Gender::feminine, 2U),
        noun_expectation(26'265U, 2U, 1U, GrammaticalCase::accusative,
                         GrammaticalNumber::singular, Gender::feminine, 2U),
        noun_expectation(26'266U, 2U, 2U, GrammaticalCase::nominative,
                         GrammaticalNumber::singular, Gender::neuter, 1U),
        noun_expectation(26'266U, 2U, 2U, GrammaticalCase::vocative,
                         GrammaticalNumber::singular, Gender::neuter, 1U),
        noun_expectation(26'266U, 2U, 2U, GrammaticalCase::genitive,
                         GrammaticalNumber::plural, Gender::neuter, 2U),
        noun_expectation(26'266U, 2U, 2U, GrammaticalCase::accusative,
                         GrammaticalNumber::singular, Gender::neuter, 2U),
        noun_expectation(26'267U, 2U, 2U, GrammaticalCase::nominative,
                         GrammaticalNumber::singular, Gender::neuter, 1U),
        noun_expectation(26'267U, 2U, 2U, GrammaticalCase::vocative,
                         GrammaticalNumber::singular, Gender::neuter, 1U),
        noun_expectation(26'267U, 2U, 2U, GrammaticalCase::genitive,
                         GrammaticalNumber::plural, Gender::neuter, 2U),
        noun_expectation(26'267U, 2U, 2U, GrammaticalCase::accusative,
                         GrammaticalNumber::singular, Gender::neuter, 2U),
        adjective_expectation(26'269U, 1U, 1U, GrammaticalCase::nominative,
                              GrammaticalNumber::singular, Gender::neuter,
                              Degree::positive, 2U),
        adjective_expectation(26'269U, 1U, 1U, GrammaticalCase::vocative,
                              GrammaticalNumber::singular, Gender::neuter,
                              Degree::positive, 2U),
        adjective_expectation(26'269U, 1U, 1U, GrammaticalCase::accusative,
                              GrammaticalNumber::singular, Gender::masculine,
                              Degree::positive, 2U),
        adjective_expectation(26'269U, 1U, 1U, GrammaticalCase::accusative,
                              GrammaticalNumber::singular, Gender::neuter,
                              Degree::positive, 2U),
    };

    expect_nominal_analyses("malum", expected, QuantityMatch::unspecified,
                            "mal", "um");
    expect_nominal_analyses("mālum", std::span{expected}.first<9U>(),
                            QuantityMatch::exact, "māl", "um");
    expect_nominal_analyses("mălum", std::span{expected}.last<8U>(),
                            QuantityMatch::exact, "măl", "um");
}

TEST(EngineTest, UnknownQuantityKeepsLegacyAnalysesAndNfcSurface) {
    const auto ascii = test::engine().analyze("servus");
    const auto marked = test::engine().analyze("servu\xCC\x84s");
    ASSERT_EQ(ascii.status, QueryStatus::analyzed);
    ASSERT_EQ(marked.status, QueryStatus::analyzed);
    EXPECT_EQ(marked.surface.normalized_nfc, "servūs");
    EXPECT_EQ(marked.analyses.size(), ascii.analyses.size());
    for (const auto &analysis : marked.analyses) {
        EXPECT_EQ(analysis.quantity_match, QuantityMatch::unknown);
    }
}

TEST(EngineTest, InflectionQuantityDistinguishesFirstDeclensionA) {
    const auto long_a = test::engine().analyze("rosā");
    const auto short_a = test::engine().analyze("rosă");
    ASSERT_EQ(long_a.status, QueryStatus::analyzed);
    ASSERT_EQ(short_a.status, QueryStatus::analyzed);

    const auto noun_readings = [](const QueryResult &result) {
        std::vector<const AnalysisIR *> readings;
        for (const auto &analysis : result.analyses) {
            if (std::holds_alternative<NounMorphology>(analysis.morphology)) {
                readings.push_back(&analysis);
            }
        }
        return readings;
    };
    const auto long_nouns = noun_readings(long_a);
    ASSERT_EQ(long_nouns.size(), 1U);
    ASSERT_FALSE(long_a.analyses.empty());
    EXPECT_EQ(&long_a.analyses.front(), long_nouns.front());
    EXPECT_EQ(std::get<NounMorphology>(long_nouns.front()->morphology)
                  .grammatical_case,
              GrammaticalCase::ablative);
    EXPECT_EQ(long_nouns.front()->quantity_match, QuantityMatch::exact);

    const auto short_nouns = noun_readings(short_a);
    ASSERT_EQ(short_nouns.size(), 2U);
    EXPECT_TRUE(
        std::ranges::all_of(short_nouns, [](const AnalysisIR *analysis) {
            const auto grammatical_case =
                std::get<NounMorphology>(analysis->morphology).grammatical_case;
            return grammatical_case == GrammaticalCase::nominative ||
                   grammatical_case == GrammaticalCase::vocative;
        }));
    EXPECT_TRUE(
        std::ranges::all_of(short_nouns, [](const AnalysisIR *analysis) {
            return analysis->quantity_match == QuantityMatch::exact;
        }));
}

TEST(EngineTest, LexicalQuantityPartitionsMalumHomographs) {
    constexpr std::uint32_t first_long_mal_entry = 26'263U;
    constexpr std::uint32_t last_long_mal_entry = 26'266U;
    constexpr std::uint32_t short_evil_entry = 26'267U;
    constexpr std::uint32_t short_bad_entry = 26'269U;

    const auto ascii = test::engine().analyze("malum");
    const auto long_a = test::engine().analyze("mālum");
    const auto short_a = test::engine().analyze("mălum");
    ASSERT_EQ(ascii.status, QueryStatus::analyzed);
    ASSERT_EQ(long_a.status, QueryStatus::analyzed);
    ASSERT_EQ(short_a.status, QueryStatus::analyzed);
    EXPECT_EQ(long_a.analyses.size() + short_a.analyses.size(),
              ascii.analyses.size());

    const auto entry_id = [](const AnalysisIR &analysis) {
        return test::engine()
                   .database()
                   .lexeme(analysis.lexeme)
                   .dictionary_entry +
               1U;
    };
    EXPECT_TRUE(std::ranges::all_of(long_a.analyses, [&](const auto &analysis) {
        const auto entry = entry_id(analysis);
        return entry >= first_long_mal_entry && entry <= last_long_mal_entry &&
               analysis.quantity_match == QuantityMatch::exact;
    }));
    EXPECT_TRUE(
        std::ranges::all_of(short_a.analyses, [&](const auto &analysis) {
            const auto entry = entry_id(analysis);
            return (entry == short_evil_entry || entry == short_bad_entry) &&
                   analysis.quantity_match == QuantityMatch::exact;
        }));
    EXPECT_TRUE(std::ranges::all_of(ascii.analyses, [](const auto &analysis) {
        return analysis.quantity_match == QuantityMatch::unspecified;
    }));
}

TEST(EngineTest, ReviewedLexicalQuantitiesSelectTheirExactLexemes) {
    const auto contains_entry = [](const QueryResult &result,
                                   const std::uint32_t dictionary_entry) {
        return std::ranges::any_of(
            result.analyses, [&](const AnalysisIR &analysis) {
                const auto &lexeme =
                    test::engine().database().lexeme(analysis.lexeme);
                return lexeme.dictionary_entry + 1U == dictionary_entry &&
                       analysis.quantity_match == QuantityMatch::exact;
            });
    };

    EXPECT_TRUE(contains_entry(test::engine().analyze("pŭella"), 32'257U));
    EXPECT_FALSE(contains_entry(test::engine().analyze("pūella"), 32'257U));
    EXPECT_TRUE(contains_entry(test::engine().analyze("ăpŭd"), 4'320U));
    EXPECT_FALSE(contains_entry(test::engine().analyze("āpŭd"), 4'320U));
    EXPECT_TRUE(contains_entry(test::engine().analyze("dēfendo"), 16'105U));
    EXPECT_FALSE(contains_entry(test::engine().analyze("dĕfendo"), 16'105U));
}

TEST(EngineTest, QuantitySeparatesMeaningDistinguishingHomographs) {
    const auto selects_only = [](const std::string_view marked,
                                 const std::uint32_t selected,
                                 const std::uint32_t rejected) {
        const auto result = test::engine().analyze(marked);
        const auto contains_entry = [&](const std::uint32_t dictionary_entry) {
            return std::ranges::any_of(
                result.analyses, [&](const AnalysisIR &analysis) {
                    const auto &lexeme =
                        test::engine().database().lexeme(analysis.lexeme);
                    return lexeme.dictionary_entry + 1U == dictionary_entry &&
                           analysis.quantity_match == QuantityMatch::exact;
                });
        };
        EXPECT_TRUE(contains_entry(selected)) << marked;
        EXPECT_FALSE(contains_entry(rejected)) << marked;
    };

    selects_only("ănăs", 3'340U, 3'341U);
    selects_only("ănās", 3'341U, 3'340U);
    selects_only("incĭdo", 23'186U, 23'187U);
    selects_only("incīdo", 23'187U, 23'186U);
    selects_only("lĕgo", 25'478U, 25'479U);
    selects_only("lēgo", 25'479U, 25'478U);
    selects_only("lĕvis", 25'590U, 25'591U);
    selects_only("lēvis", 25'591U, 25'590U);
    selects_only("lĭber", 25'632U, 25'630U);
    selects_only("līber", 25'630U, 25'632U);
    selects_only("occĭdo", 28'484U, 28'485U);
    selects_only("occīdo", 28'485U, 28'484U);
    selects_only("pŏpulus", 30'955U, 30'957U);
    selects_only("pōpulus", 30'957U, 30'955U);
    selects_only("tŭber", 37'919U, 37'922U);
    selects_only("tūber", 37'922U, 37'919U);
}

TEST(EngineTest, RejectsCharactersThatOnlyCaseFoldIntoLatinAscii) {
    for (const std::string_view text : {"ß", "K", "ſ"}) {
        const auto result = test::engine().analyze(text);
        EXPECT_EQ(result.status, QueryStatus::error) << text;
        EXPECT_TRUE(result.analyses.empty()) << text;
        ASSERT_EQ(result.diagnostics.size(), 1U) << text;
        EXPECT_EQ(result.diagnostics.front().code,
                  DiagnosticCode::unsupported_character)
            << text;
    }
}

TEST(EngineTest, EmitsBothJsonEnvelopes) {
    const auto result = test::engine().analyze("servus");
    const auto full = Json::parse(analysis_json(test::engine(), result));
    const auto search = Json::parse(search_json(test::engine(), result));
    EXPECT_EQ(full.at("schema"), "whitakers-words.analysis");
    EXPECT_EQ(full.at("analyses").size(), 1U);
    EXPECT_EQ(search.at("schema"), "whitakers-words.search");
    EXPECT_EQ(search.at("hits").size(), 1U);
    EXPECT_EQ(search.at("datasetId"), test::dataset_id);
}

TEST(EngineTest, EmitsCanonicalAdjectiveShape) {
    const auto result = test::engine().analyze("pulcher");
    const auto full = Json::parse(analysis_json(test::engine(), result));
    ASSERT_EQ(full.at("analyses").size(), 2U);
    const auto &analysis = full.at("analyses").front();
    EXPECT_EQ(analysis.at("partOfSpeech"), "adjective");
    EXPECT_EQ(analysis.at("lexeme").at("properties").at("degree"), nullptr);
    EXPECT_EQ(analysis.at("morphology").at("degree"), "positive");
}

TEST(EngineTest, AppliesExactlyOneDataDrivenSuffix) {
    const auto first = test::engine().analyze("anaticulus");
    ASSERT_EQ(first.status, QueryStatus::analyzed);
    ASSERT_EQ(first.analyses.size(), 2U);
    EXPECT_TRUE(
        std::ranges::all_of(first.analyses, [](const AnalysisIR &analysis) {
            return analysis.derivation.count == 1U;
        }));

    const auto second = test::engine().analyze("anaticuliculus");
    ASSERT_EQ(second.status, QueryStatus::analyzed);
    ASSERT_EQ(second.analyses.size(), 1U);
    EXPECT_EQ(second.analyses.front().derivation.count, 1U);

    const auto third = test::engine().analyze("anaticuliculiculus");
    EXPECT_EQ(third.status, QueryStatus::unknown);
    EXPECT_TRUE(third.analyses.empty());
}

TEST(EngineTest, EmitsSuffixInFullAndSearchJson) {
    const auto result = test::engine().analyze("anaticulus");
    const auto full = Json::parse(analysis_json(test::engine(), result));
    const auto search = Json::parse(search_json(test::engine(), result));

    ASSERT_EQ(full.at("analyses").size(), 2U);
    const auto &derivation = full.at("analyses").front().at("derivation");
    EXPECT_EQ(derivation.at("method"), "derived");
    ASSERT_EQ(derivation.at("steps").size(), 1U);
    EXPECT_EQ(derivation.at("steps").front().at("text"), "icul");
    EXPECT_FALSE(derivation.at("steps").front().contains("connector"));

    ASSERT_EQ(search.at("hits").size(), 2U);
    EXPECT_TRUE(std::ranges::all_of(search.at("hits"), [](const Json &hit) {
        return hit.at("addonIds").size() == 1U;
    }));
}

TEST(EngineTest, AppliesOnePrefixIncludingConnectorConstraint) {
    for (const auto *const word : {"archipuella", "appuella"}) {
        const auto result = test::engine().analyze(word);
        ASSERT_EQ(result.status, QueryStatus::analyzed) << word;
        ASSERT_EQ(result.analyses.size(), 3U) << word;
        EXPECT_TRUE(std::ranges::all_of(
            result.analyses, [&](const AnalysisIR &analysis) {
                const auto steps = analysis.derivation.steps();
                return steps.size() == 1U &&
                       test::engine().database().addon_kind(steps.front()) ==
                           AddonKind::prefix;
            }));
    }

    const auto nested = test::engine().analyze("archiarchipuella");
    EXPECT_EQ(nested.status, QueryStatus::unknown);
}

TEST(EngineTest, ComposesOnePrefixAndOneSuffixInCanonicalOrder) {
    const auto result = test::engine().analyze("archipuellulus");
    ASSERT_EQ(result.status, QueryStatus::analyzed);
    ASSERT_EQ(result.analyses.size(), 4U);
    EXPECT_TRUE(
        std::ranges::all_of(result.analyses, [&](const AnalysisIR &analysis) {
            const auto steps = analysis.derivation.steps();
            return steps.size() == 2U &&
                   test::engine().database().addon_kind(steps[0]) ==
                       AddonKind::prefix &&
                   test::engine().database().addon_kind(steps[1]) ==
                       AddonKind::suffix;
        }));

    const auto full = Json::parse(analysis_json(test::engine(), result));
    for (const auto &analysis : full.at("analyses")) {
        const auto &steps = analysis.at("derivation").at("steps");
        ASSERT_EQ(steps.size(), 2U);
        EXPECT_EQ(steps[0].at("type"), "prefix");
        EXPECT_EQ(steps[1].at("type"), "suffix");
        EXPECT_EQ(analysis.at("form").at("stem"), "puellul");
    }
}

TEST(EngineTest, AppliesEncliticBeforeExistingDerivation) {
    const auto plain = test::engine().analyze("puellaque");
    ASSERT_EQ(plain.status, QueryStatus::analyzed);
    ASSERT_EQ(plain.analyses.size(), 3U);
    EXPECT_TRUE(
        std::ranges::all_of(plain.analyses, [&](const AnalysisIR &analysis) {
            const auto steps = analysis.derivation.steps();
            return steps.size() == 1U && test::engine().database().addon_kind(
                                             steps[0]) == AddonKind::tackon;
        }));

    const auto prefixed = test::engine().analyze("archipuellaque");
    ASSERT_EQ(prefixed.status, QueryStatus::analyzed);
    ASSERT_EQ(prefixed.analyses.size(), 3U);
    EXPECT_TRUE(
        std::ranges::all_of(prefixed.analyses, [&](const AnalysisIR &analysis) {
            const auto steps = analysis.derivation.steps();
            return steps.size() == 2U &&
                   test::engine().database().addon_kind(steps[0]) ==
                       AddonKind::tackon &&
                   test::engine().database().addon_kind(steps[1]) ==
                       AddonKind::prefix;
        }));

    const auto suffixed = test::engine().analyze("anaticulusque");
    ASSERT_EQ(suffixed.status, QueryStatus::analyzed);
    ASSERT_EQ(suffixed.analyses.size(), 2U);
    EXPECT_TRUE(
        std::ranges::all_of(suffixed.analyses, [&](const AnalysisIR &analysis) {
            const auto steps = analysis.derivation.steps();
            return !analysis.derivation.rewritten_form.has_value() &&
                   steps.size() == 2U &&
                   test::engine().database().addon_kind(steps[0]) ==
                       AddonKind::tackon &&
                   test::engine().database().addon_kind(steps[1]) ==
                       AddonKind::suffix;
        }));
}

TEST(EngineTest, KeepsDirectEncliticAnalysisAheadOfSpellingRecovery) {
    const auto result = test::engine().analyze("aequataque");
    ASSERT_EQ(result.status, QueryStatus::analyzed);
    ASSERT_EQ(result.analyses.size(), 6U);
    EXPECT_TRUE(
        std::ranges::all_of(result.analyses, [&](const AnalysisIR &analysis) {
            const auto steps = analysis.derivation.steps();
            return !analysis.derivation.rewritten_form.has_value() &&
                   steps.size() == 1U &&
                   test::engine().database().addon_kind(steps.front()) ==
                       AddonKind::tackon;
        }));

    const auto full = Json::parse(analysis_json(test::engine(), result));
    EXPECT_TRUE(std::ranges::all_of(full.at("analyses"), [](const Json &item) {
        return item.at("lexeme").at("entryId") == 1938 &&
               item.at("derivation").at("method") == "derived";
    }));
}

TEST(EngineTest, SchedulesDirectWordsSuffixesAndEncliticsByStrength) {
    const auto is_suffix_adverb = [&](const AnalysisIR &analysis) {
        return std::holds_alternative<AdverbMorphology>(analysis.morphology) &&
               std::ranges::any_of(
                   analysis.derivation.steps(), [&](const AddonId id) {
                       return test::engine().database().addon_kind(id) ==
                              AddonKind::suffix;
                   });
    };
    constexpr std::array<std::string_view, 8> derived_adverbs{
        "corde", "die",    "honore",  "improbe",
        "nate",  "oblite", "perfide", "sancte",
    };
    for (const std::string_view word : derived_adverbs) {
        const auto result = test::engine().analyze(word);
        EXPECT_TRUE(std::ranges::any_of(result.analyses, is_suffix_adverb))
            << word;
    }
    constexpr std::array<std::string_view, 5> rejected_adverbs{
        "forte", "adsidue", "late", "male", "sole"};
    for (const std::string_view word : rejected_adverbs) {
        const auto result = test::engine().analyze(word);
        EXPECT_TRUE(std::ranges::none_of(result.analyses, is_suffix_adverb))
            << word;
    }

    const auto nequiquam = test::engine().analyze("nequiquam");
    EXPECT_TRUE(
        std::ranges::any_of(nequiquam.analyses, [](const AnalysisIR &analysis) {
            return std::holds_alternative<AdverbMorphology>(
                       analysis.morphology) &&
                   analysis.derivation.count == 0U;
        }));

    const auto solane = test::engine().analyze("solane");
    ASSERT_FALSE(solane.analyses.empty());
    EXPECT_TRUE(
        std::ranges::all_of(solane.analyses, [&](const AnalysisIR &analysis) {
            return analysis.derivation.count == 1U &&
                   test::engine().database().addon_kind(
                       analysis.derivation.steps().front()) ==
                       AddonKind::tackon;
        }));

    const auto mixtique = test::engine().analyze("mixtique");
    ASSERT_FALSE(mixtique.analyses.empty());
    EXPECT_TRUE(
        std::ranges::all_of(mixtique.analyses, [](const AnalysisIR &analysis) {
            return std::holds_alternative<ParticipleMorphology>(
                       analysis.morphology) &&
                   !analysis.derivation.rewritten_form.has_value();
        }));
}

TEST(EngineTest, KeepsCanonicalQuocumqueReadingsWithoutDuplicatePaths) {
    const auto result = test::engine().analyze("quocumque");
    ASSERT_EQ(result.status, QueryStatus::analyzed);
    ASSERT_EQ(result.analyses.size(), 3U);
    EXPECT_EQ(std::ranges::count_if(
                  result.analyses,
                  [](const AnalysisIR &analysis) {
                      return std::holds_alternative<PronounMorphology>(
                          analysis.morphology);
                  }),
              2U);
    EXPECT_EQ(std::ranges::count_if(
                  result.analyses,
                  [](const AnalysisIR &analysis) {
                      return std::holds_alternative<AdverbMorphology>(
                          analysis.morphology);
                  }),
              1U);
}

TEST(EngineTest, ExposesFourthConjugationAsPublicParadigm) {
    const auto result = test::engine().analyze("audiam");
    ASSERT_EQ(result.status, QueryStatus::analyzed);
    ASSERT_EQ(result.analyses.size(), 2U);
    EXPECT_TRUE(
        std::ranges::all_of(result.analyses, [](const AnalysisIR &analysis) {
            const auto *verb =
                std::get_if<VerbMorphology>(&analysis.morphology);
            return verb != nullptr && verb->conjugation == 4U &&
                   verb->variant == 1U;
        }));
}

TEST(EngineTest, CoversAliquPronounsAndDeduplicatesCuiquePackons) {
    const auto aliquis = test::engine().analyze("aliquis");
    ASSERT_EQ(aliquis.status, QueryStatus::analyzed);
    ASSERT_EQ(aliquis.analyses.size(), 5U);
    EXPECT_TRUE(
        std::ranges::all_of(aliquis.analyses, [](const AnalysisIR &analysis) {
            return std::holds_alternative<PronounMorphology>(
                analysis.morphology);
        }));

    const auto cuique = test::engine().analyze("cuique");
    ASSERT_EQ(cuique.status, QueryStatus::analyzed);
    ASSERT_EQ(cuique.analyses.size(), 9U);
    const auto addon_count = [&](const AddonKind kind) {
        return std::ranges::count_if(
            cuique.analyses, [&](const AnalysisIR &analysis) {
                const auto steps = analysis.derivation.steps();
                return steps.size() == 1U &&
                       test::engine().database().addon_kind(steps.front()) ==
                           kind;
            });
    };
    EXPECT_EQ(addon_count(AddonKind::tackon), 5U);
    EXPECT_EQ(addon_count(AddonKind::packon), 4U);
}

TEST(EngineTest, AppliesPackonWithOptionalTickon) {
    for (const auto *const word : {"quidam", "ecquidam"}) {
        const auto result = test::engine().analyze(word);
        ASSERT_EQ(result.status, QueryStatus::analyzed) << word;
        ASSERT_EQ(result.analyses.size(), 3U) << word;
        EXPECT_TRUE(std::ranges::all_of(
            result.analyses, [](const AnalysisIR &analysis) {
                return std::holds_alternative<PronounMorphology>(
                    analysis.morphology);
            }));
    }

    const auto prefixed = test::engine().analyze("ecquidam");
    const auto full = Json::parse(analysis_json(test::engine(), prefixed));
    for (const auto &analysis : full.at("analyses")) {
        const auto &steps = analysis.at("derivation").at("steps");
        ASSERT_EQ(steps.size(), 2U);
        EXPECT_EQ(steps[0].at("type"), "prefix");
        EXPECT_EQ(steps[1].at("type"), "packon");
    }
}

} // namespace words
