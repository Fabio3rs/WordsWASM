#include "test_support.hpp"

#include "words/json.hpp"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <ranges>
#include <string_view>

namespace words {

using Json = nlohmann::ordered_json;

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
    EXPECT_EQ(result.diagnostics.back().code, "two-words-suggestion");

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

TEST(EngineTest, AppliesDataDrivenPerfectSyncopeByPriority) {
    constexpr std::array<std::pair<std::string_view, std::string_view>, 4>
        fixtures{{
            {"amasti", "perfect-v-contraction"},
            {"amarunt", "perfect-v-before-r"},
            {"audisti", "perfect-v-contraction"},
            {"audiisti", "perfect-ivi-uncontracted"},
        }};
    for (const auto &[word, rule_name] : fixtures) {
        const auto result = test::engine().analyze(word);
        ASSERT_EQ(result.status, QueryStatus::analyzed) << word;
        ASSERT_EQ(result.analyses.size(), 1U) << word;
        const auto &rewritten =
            result.analyses.front().derivation.rewritten_form;
        ASSERT_TRUE(rewritten.has_value()) << word;

        const auto full = Json::parse(analysis_json(test::engine(), result));
        const auto &derivation = full.at("analyses").front().at("derivation");
        EXPECT_EQ(derivation.at("method"), "syncope") << word;
        ASSERT_EQ(derivation.at("steps").size(), 1U) << word;
        EXPECT_EQ(derivation.at("steps").front().at("rule"), rule_name) << word;

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
    for (const auto word : {"amavisti", "servus", "zzzzaszz"}) {
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
            EXPECT_EQ(analysis.at("derivation").at("steps").front().at("rule"),
                      rule_name)
                << word;
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
        std::uint8_t person;
        GrammaticalNumber number;
    };
    constexpr std::array fixtures{
        Fixture{"amata est", CompoundKind::finite_sum, Tense::perfect,
                Voice::passive, Mood::indicative, 3U,
                GrammaticalNumber::singular},
        Fixture{"amati sunt", CompoundKind::finite_sum, Tense::perfect,
                Voice::passive, Mood::indicative, 3U,
                GrammaticalNumber::plural},
        Fixture{"amata fuerit", CompoundKind::finite_sum, Tense::unknown,
                Voice::passive, Mood::indicative, 3U,
                GrammaticalNumber::singular},
        Fixture{"amaturus est", CompoundKind::finite_sum, Tense::present,
                Voice::passive, Mood::indicative, 3U,
                GrammaticalNumber::singular},
        Fixture{"amatus esse", CompoundKind::esse, Tense::perfect,
                Voice::passive, Mood::infinitive, 0U,
                GrammaticalNumber::unknown},
        Fixture{"amaturus fuisse", CompoundKind::fuisse, Tense::perfect,
                Voice::active, Mood::infinitive, 0U,
                GrammaticalNumber::unknown},
        Fixture{"amatum iri", CompoundKind::iri, Tense::future, Voice::passive,
                Mood::infinitive, 0U, GrammaticalNumber::unknown},
    };

    for (const auto &fixture : fixtures) {
        const auto result = test::engine().analyze_text(fixture.text);
        ASSERT_EQ(result.status, QueryStatus::analyzed) << fixture.text;
        ASSERT_EQ(result.analyses.size(), 1U) << fixture.text;
        ASSERT_EQ(result.compound_analyses.size(), 1U) << fixture.text;
        const auto &compound = result.compound_analyses.front();
        EXPECT_EQ(compound.kind, fixture.kind) << fixture.text;
        EXPECT_EQ(compound.morphology.tense, fixture.tense) << fixture.text;
        EXPECT_EQ(compound.morphology.voice, fixture.voice) << fixture.text;
        EXPECT_EQ(compound.morphology.mood, fixture.mood) << fixture.text;
        EXPECT_EQ(compound.morphology.person, fixture.person) << fixture.text;
        EXPECT_EQ(compound.morphology.number, fixture.number) << fixture.text;

        const auto full = Json::parse(analysis_json(test::engine(), result));
        ASSERT_EQ(full.at("analyses").size(), 2U) << fixture.text;
        EXPECT_EQ(full.at("query").at("normalized"), fixture.text);
        const auto &derivation = full.at("analyses").back().at("derivation");
        EXPECT_EQ(derivation.at("method"), "compound") << fixture.text;
        ASSERT_EQ(derivation.at("steps").size(), 1U) << fixture.text;
        EXPECT_EQ(derivation.at("steps").front().at("type"), "compound");

        const auto search = Json::parse(search_json(test::engine(), result));
        ASSERT_EQ(search.at("hits").size(), 2U) << fixture.text;
        EXPECT_EQ(std::ranges::count_if(
                      search.at("hits"),
                      [](const Json &hit) { return hit.contains("compound"); }),
                  1U);
    }
}

TEST(EngineTest, RejectsGeneralPhraseParsingOutsideCompoundGrammar) {
    for (const auto text : {"amata amat", "puella amat servum"}) {
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

TEST(EngineTest, MacronKeepsLegacyAnalysesAndNfcSurface) {
    const auto ascii = test::engine().analyze("puella");
    const auto marked = test::engine().analyze("puella\xCC\x84");
    ASSERT_EQ(ascii.status, QueryStatus::analyzed);
    ASSERT_EQ(marked.status, QueryStatus::analyzed);
    EXPECT_EQ(marked.surface.normalized_nfc, "puellā");
    EXPECT_EQ(marked.analyses.size(), ascii.analyses.size());
    for (const auto &analysis : marked.analyses) {
        EXPECT_EQ(analysis.quantity_match, QuantityMatch::unknown);
        EXPECT_EQ(marked.surface.slice(analysis.ending), "ā");
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
    for (const auto word : {"archipuella", "appuella"}) {
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
    for (const auto word : {"quidam", "ecquidam"}) {
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
