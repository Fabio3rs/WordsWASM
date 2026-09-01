#include "test_support.hpp"

#include "words/database.hpp"
#include "words/semantics.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <ranges>
#include <type_traits>
#include <utility>

namespace words {
namespace {

constexpr std::size_t test_header_major_offset = 8U;
constexpr std::size_t test_header_minor_offset = 10U;
constexpr std::size_t test_header_size_offset = 12U;
constexpr std::size_t test_header_section_count_offset = 16U;
constexpr std::size_t test_directory_offset = 40U;
constexpr std::size_t test_directory_flags_offset = test_directory_offset + 4U;

void write_u16_le(std::vector<std::byte> &bytes, const std::size_t offset,
                  const std::uint16_t value) {
    bytes[offset] = static_cast<std::byte>(value & 0xffU);
    bytes[offset + 1U] = static_cast<std::byte>(value >> 8U);
}

void write_u32_le(std::vector<std::byte> &bytes, const std::size_t offset,
                  const std::uint32_t value) {
    bytes[offset] = static_cast<std::byte>(value & 0xffU);
    bytes[offset + 1U] = static_cast<std::byte>((value >> 8U) & 0xffU);
    bytes[offset + 2U] = static_cast<std::byte>((value >> 16U) & 0xffU);
    bytes[offset + 3U] = static_cast<std::byte>(value >> 24U);
}

} // namespace

TEST(DatabaseTest, LoadsDensePocAndFindsRealData) {
    auto database = Database::load_poc(test::read_database());
    ASSERT_TRUE(database) << database.error().message;
    EXPECT_FALSE((*database)->lookup_stem("puell").empty());
    EXPECT_FALSE((*database)->lookup_ending("ae").empty());
    EXPECT_FALSE((*database)->lookup_ending("").empty());
    EXPECT_TRUE((*database)->lookup_stem("zzzzzz").empty());
}

TEST(DatabaseTest, LoadsColumnarSearchPocWithoutMeaningPools) {
    auto database = Database::load_poc(test::read_search_database());
    ASSERT_TRUE(database) << database.error().message;
    EXPECT_EQ((*database)->content(), DatabaseContent::search);
    EXPECT_FALSE((*database)->has_meanings());
    EXPECT_FALSE((*database)->lookup_stem("puell").empty());
    EXPECT_FALSE((*database)->lookup_ending("ae").empty());
    EXPECT_EQ((*database)->lookup_unique("eadem").size(), 3U);
    EXPECT_EQ((*database)->lookup_suffix("icul").size(), 3U);
}

TEST(DatabaseTest, DenseAndSearchProfilesAgreeOnWireSemantics) {
    auto dense = Database::load_poc(test::read_database());
    auto search = Database::load_poc(test::read_search_database());
    ASSERT_TRUE(dense) << dense.error().message;
    ASSERT_TRUE(search) << search.error().message;

    const auto dense_stems = (*dense)->lookup_stem("puell");
    const auto search_stems = (*search)->lookup_stem("puell");
    ASSERT_FALSE(dense_stems.empty());
    const auto matching_stem = std::ranges::find(
        search_stems, dense_stems.front().lexeme, &StemReference::lexeme);
    ASSERT_NE(matching_stem, search_stems.end());
    EXPECT_EQ(matching_stem->lexical_slot, dense_stems.front().lexical_slot);
    EXPECT_EQ(matching_stem->stem_key, dense_stems.front().stem_key);
    const auto &dense_lexeme = (*dense)->lexeme(dense_stems.front().lexeme);
    const auto &search_lexeme = (*search)->lexeme(matching_stem->lexeme);
    EXPECT_EQ(search_lexeme.stems, dense_lexeme.stems);
    EXPECT_EQ(search_lexeme.part_of_speech, dense_lexeme.part_of_speech);
    EXPECT_EQ(search_lexeme.declension, dense_lexeme.declension);
    EXPECT_EQ(search_lexeme.variant, dense_lexeme.variant);
    EXPECT_EQ(search_lexeme.gender, dense_lexeme.gender);
    EXPECT_EQ(search_lexeme.noun_kind, dense_lexeme.noun_kind);
    EXPECT_EQ(search_lexeme.age, dense_lexeme.age);
    EXPECT_EQ(search_lexeme.subject, dense_lexeme.subject);
    EXPECT_EQ(search_lexeme.geography, dense_lexeme.geography);
    EXPECT_EQ(search_lexeme.frequency, dense_lexeme.frequency);
    EXPECT_EQ(search_lexeme.source, dense_lexeme.source);

    constexpr RuleId rule_id{146U};
    const auto &dense_rule = (*dense)->rule(rule_id);
    const auto &search_rule = (*search)->rule(rule_id);
    EXPECT_EQ(search_rule.part_of_speech, dense_rule.part_of_speech);
    EXPECT_EQ(search_rule.declension, dense_rule.declension);
    EXPECT_EQ(search_rule.variant, dense_rule.variant);
    EXPECT_EQ(search_rule.grammatical_case, dense_rule.grammatical_case);
    EXPECT_EQ(search_rule.number, dense_rule.number);
    EXPECT_EQ(search_rule.gender, dense_rule.gender);
    EXPECT_EQ(search_rule.ending, dense_rule.ending);
    EXPECT_EQ(search_rule.stem_key, dense_rule.stem_key);
    EXPECT_EQ(search_rule.age, dense_rule.age);
    EXPECT_EQ(search_rule.frequency, dense_rule.frequency);
    EXPECT_EQ((*search)->inflection_quantity(rule_id).known,
              (*dense)->inflection_quantity(rule_id).known);
    EXPECT_EQ((*search)->inflection_quantity(rule_id).long_vowel,
              (*dense)->inflection_quantity(rule_id).long_vowel);

    const auto compare_addon =
        [&](const std::string_view spelling, const auto dense_lookup,
            const auto search_lookup, const auto accessor) {
            const auto dense_ids = ((*dense).get()->*dense_lookup)(spelling);
            const auto search_ids = ((*search).get()->*search_lookup)(spelling);
            ASSERT_FALSE(dense_ids.empty());
            ASSERT_NE(std::ranges::find(search_ids, dense_ids.front()),
                      search_ids.end());
            const auto &dense_rule_value =
                ((*dense).get()->*accessor)(dense_ids.front());
            const auto &search_rule_value =
                ((*search).get()->*accessor)(dense_ids.front());
            EXPECT_EQ(search_rule_value.id, dense_rule_value.id);
            EXPECT_EQ(search_rule_value.fix, dense_rule_value.fix);
        };
    compare_addon("icul", &Database::lookup_suffix, &Database::lookup_suffix,
                  &Database::suffix);
    compare_addon("archi", &Database::lookup_prefix, &Database::lookup_prefix,
                  &Database::prefix);
    compare_addon("que", &Database::lookup_tackon, &Database::lookup_tackon,
                  &Database::tackon);
    compare_addon("dam", &Database::lookup_packon, &Database::lookup_packon,
                  &Database::tackon);

    const auto suffix_id = (*dense)->lookup_suffix("icul").front();
    const auto &dense_suffix = (*dense)->suffix(suffix_id);
    const auto &search_suffix = (*search)->suffix(suffix_id);
    EXPECT_EQ(search_suffix.root, dense_suffix.root);
    EXPECT_EQ(search_suffix.root_key, dense_suffix.root_key);
    EXPECT_EQ(search_suffix.target, dense_suffix.target);
    EXPECT_EQ(search_suffix.target_key, dense_suffix.target_key);
    EXPECT_EQ(search_suffix.target_declension, dense_suffix.target_declension);
    EXPECT_EQ(search_suffix.target_variant, dense_suffix.target_variant);
    EXPECT_EQ(search_suffix.target_noun_kind, dense_suffix.target_noun_kind);
    EXPECT_EQ(search_suffix.numeric_value, dense_suffix.numeric_value);
    EXPECT_EQ(search_suffix.connector, dense_suffix.connector);

    const auto prefix_id = (*dense)->lookup_prefix("archi").front();
    const auto &dense_prefix = (*dense)->prefix(prefix_id);
    const auto &search_prefix = (*search)->prefix(prefix_id);
    EXPECT_EQ(search_prefix.root, dense_prefix.root);
    EXPECT_EQ(search_prefix.target, dense_prefix.target);
    EXPECT_EQ(search_prefix.connector, dense_prefix.connector);

    const auto compare_tackon_fields = [&](const AddonId id) {
        const auto &dense_tackon = (*dense)->tackon(id);
        const auto &search_tackon = (*search)->tackon(id);
        EXPECT_EQ(search_tackon.base, dense_tackon.base);
        EXPECT_EQ(search_tackon.declension, dense_tackon.declension);
        EXPECT_EQ(search_tackon.variant, dense_tackon.variant);
        EXPECT_EQ(search_tackon.gender, dense_tackon.gender);
        EXPECT_EQ(search_tackon.noun_kind, dense_tackon.noun_kind);
        EXPECT_EQ(search_tackon.pronoun_kind, dense_tackon.pronoun_kind);
        EXPECT_EQ(search_tackon.adjective_degree,
                  dense_tackon.adjective_degree);
        EXPECT_EQ(search_tackon.packon, dense_tackon.packon);
        EXPECT_EQ(search_tackon.enclitic, dense_tackon.enclitic);
    };
    compare_tackon_fields((*dense)->lookup_tackon("que").front());
    compare_tackon_fields((*dense)->lookup_packon("dam").front());

    const auto &dense_rewrite = (*dense)->rewrite(RewriteId{0U});
    const auto &search_rewrite = (*search)->rewrite(RewriteId{0U});
    EXPECT_EQ(search_rewrite.id, dense_rewrite.id);
    EXPECT_EQ(search_rewrite.before, dense_rewrite.before);
    EXPECT_EQ(search_rewrite.after, dense_rewrite.after);
    EXPECT_EQ(search_rewrite.name, dense_rewrite.name);
    EXPECT_EQ(search_rewrite.kind, dense_rewrite.kind);
    EXPECT_EQ(search_rewrite.scope, dense_rewrite.scope);
    EXPECT_EQ(search_rewrite.priority, dense_rewrite.priority);
    EXPECT_EQ(search_rewrite.scan_reverse, dense_rewrite.scan_reverse);
    EXPECT_EQ(search_rewrite.required_part, dense_rewrite.required_part);
    EXPECT_EQ(search_rewrite.required_stem_key,
              dense_rewrite.required_stem_key);
    EXPECT_EQ(search_rewrite.minimum_before, dense_rewrite.minimum_before);
    EXPECT_EQ(search_rewrite.minimum_after, dense_rewrite.minimum_after);
    EXPECT_EQ(search_rewrite.medieval, dense_rewrite.medieval);
    EXPECT_EQ(search_rewrite.operation, dense_rewrite.operation);
    EXPECT_EQ(search_rewrite.stage, dense_rewrite.stage);
    EXPECT_EQ(search_rewrite.constraint, dense_rewrite.constraint);

    const auto dense_uniques = (*dense)->lookup_unique("eadem");
    const auto search_uniques = (*search)->lookup_unique("eadem");
    ASSERT_EQ(search_uniques.size(), dense_uniques.size());
    EXPECT_TRUE(std::ranges::equal(dense_uniques, search_uniques, {},
                                   &UniqueReference::lexeme,
                                   &UniqueReference::lexeme));
    for (std::size_t index = 0; index < dense_uniques.size(); ++index) {
        const auto &dense_morphology =
            std::get<PronounMorphology>(dense_uniques[index].morphology);
        const auto &search_morphology =
            std::get<PronounMorphology>(search_uniques[index].morphology);
        EXPECT_EQ(search_morphology.declension, dense_morphology.declension);
        EXPECT_EQ(search_morphology.variant, dense_morphology.variant);
        EXPECT_EQ(search_morphology.grammatical_case,
                  dense_morphology.grammatical_case);
        EXPECT_EQ(search_morphology.number, dense_morphology.number);
        EXPECT_EQ(search_morphology.gender, dense_morphology.gender);
    }
}

TEST(DatabaseTest, FullAndSearchAgreeOnEveryGrammaticalRecord) {
    auto full = Database::load_poc(test::read_database());
    auto search = Database::load_poc(test::read_search_database());
    ASSERT_TRUE(full) << full.error().message;
    ASSERT_TRUE(search) << search.error().message;

    const auto full_lexemes = (*full)->lexemes();
    const auto search_lexemes = (*search)->lexemes();
    ASSERT_EQ(search_lexemes.size(), full_lexemes.size());
    for (std::size_t index = 0; index < full_lexemes.size(); ++index) {
        SCOPED_TRACE(index);
        const auto &left = full_lexemes[index];
        const auto &right = search_lexemes[index];
        EXPECT_EQ(right.stems, left.stems);
        EXPECT_EQ(right.dictionary, left.dictionary);
        EXPECT_EQ(right.dictionary_entry, left.dictionary_entry);
        EXPECT_EQ(right.part_of_speech, left.part_of_speech);
        EXPECT_EQ(right.declension, left.declension);
        EXPECT_EQ(right.variant, left.variant);
        EXPECT_EQ(right.gender, left.gender);
        EXPECT_EQ(right.noun_kind, left.noun_kind);
        EXPECT_EQ(right.pronoun_kind, left.pronoun_kind);
        EXPECT_EQ(right.required_packon, left.required_packon);
        EXPECT_EQ(right.adjective_degree, left.adjective_degree);
        EXPECT_EQ(right.numeral_type, left.numeral_type);
        EXPECT_EQ(right.numeral_value, left.numeral_value);
        EXPECT_EQ(right.adverb_degree, left.adverb_degree);
        EXPECT_EQ(right.verb_kind, left.verb_kind);
        EXPECT_EQ(right.governs, left.governs);
        EXPECT_EQ(right.age, left.age);
        EXPECT_EQ(right.subject, left.subject);
        EXPECT_EQ(right.geography, left.geography);
        EXPECT_EQ(right.frequency, left.frequency);
        EXPECT_EQ(right.source, left.source);
        for (std::uint8_t slot = 0; slot < 4U; ++slot) {
            EXPECT_EQ((*search)
                          ->stem_quantity(
                              LexemeId{static_cast<std::uint32_t>(index)}, slot)
                          .known,
                      (*full)
                          ->stem_quantity(
                              LexemeId{static_cast<std::uint32_t>(index)}, slot)
                          .known);
            EXPECT_EQ((*search)
                          ->stem_quantity(
                              LexemeId{static_cast<std::uint32_t>(index)}, slot)
                          .long_vowel,
                      (*full)
                          ->stem_quantity(
                              LexemeId{static_cast<std::uint32_t>(index)}, slot)
                          .long_vowel);
        }
    }

    const auto full_rules = (*full)->rules();
    const auto search_rules = (*search)->rules();
    ASSERT_EQ(search_rules.size(), full_rules.size());
    for (std::size_t index = 0; index < full_rules.size(); ++index) {
        SCOPED_TRACE(index);
        const auto &left = full_rules[index];
        const auto &right = search_rules[index];
        EXPECT_EQ(right.id, left.id);
        EXPECT_EQ(right.part_of_speech, left.part_of_speech);
        EXPECT_EQ(right.declension, left.declension);
        EXPECT_EQ(right.variant, left.variant);
        EXPECT_EQ(right.grammatical_case, left.grammatical_case);
        EXPECT_EQ(right.number, left.number);
        EXPECT_EQ(right.gender, left.gender);
        EXPECT_EQ(right.adjective_degree, left.adjective_degree);
        EXPECT_EQ(right.numeral_type, left.numeral_type);
        EXPECT_EQ(right.tense, left.tense);
        EXPECT_EQ(right.voice, left.voice);
        EXPECT_EQ(right.mood, left.mood);
        EXPECT_EQ(right.person, left.person);
        EXPECT_EQ(right.ending, left.ending);
        EXPECT_EQ(right.stem_key, left.stem_key);
        EXPECT_EQ(right.age, left.age);
        EXPECT_EQ(right.frequency, left.frequency);
        EXPECT_EQ((*search)->inflection_quantity(right.id).known,
                  (*full)->inflection_quantity(left.id).known);
        EXPECT_EQ((*search)->inflection_quantity(right.id).long_vowel,
                  (*full)->inflection_quantity(left.id).long_vowel);
    }

    const auto full_suffixes = (*full)->suffixes();
    const auto search_suffixes = (*search)->suffixes();
    ASSERT_EQ(search_suffixes.size(), full_suffixes.size());
    for (std::size_t index = 0; index < full_suffixes.size(); ++index) {
        const auto &left = full_suffixes[index];
        const auto &right = search_suffixes[index];
        EXPECT_EQ(right.id, left.id);
        EXPECT_EQ(right.fix, left.fix);
        EXPECT_EQ(right.root, left.root);
        EXPECT_EQ(right.root_key, left.root_key);
        EXPECT_EQ(right.target, left.target);
        EXPECT_EQ(right.target_key, left.target_key);
        EXPECT_EQ(right.target_declension, left.target_declension);
        EXPECT_EQ(right.target_variant, left.target_variant);
        EXPECT_EQ(right.target_gender, left.target_gender);
        EXPECT_EQ(right.target_noun_kind, left.target_noun_kind);
        EXPECT_EQ(right.target_degree, left.target_degree);
        EXPECT_EQ(right.target_numeral_type, left.target_numeral_type);
        EXPECT_EQ(right.numeric_value, left.numeric_value);
        EXPECT_EQ(right.connector, left.connector);
    }

    const auto full_prefixes = (*full)->prefixes();
    const auto search_prefixes = (*search)->prefixes();
    ASSERT_EQ(search_prefixes.size(), full_prefixes.size());
    for (std::size_t index = 0; index < full_prefixes.size(); ++index) {
        const auto &left = full_prefixes[index];
        const auto &right = search_prefixes[index];
        EXPECT_EQ(right.id, left.id);
        EXPECT_EQ(right.fix, left.fix);
        EXPECT_EQ(right.root, left.root);
        EXPECT_EQ(right.target, left.target);
        EXPECT_EQ(right.connector, left.connector);
        EXPECT_EQ((*search)->addon_kind(right.id),
                  (*full)->addon_kind(left.id));
    }

    const auto full_tackons = (*full)->tackons();
    const auto search_tackons = (*search)->tackons();
    ASSERT_EQ(search_tackons.size(), full_tackons.size());
    for (std::size_t index = 0; index < full_tackons.size(); ++index) {
        const auto &left = full_tackons[index];
        const auto &right = search_tackons[index];
        EXPECT_EQ(right.id, left.id);
        EXPECT_EQ(right.fix, left.fix);
        EXPECT_EQ(right.base, left.base);
        EXPECT_EQ(right.declension, left.declension);
        EXPECT_EQ(right.variant, left.variant);
        EXPECT_EQ(right.gender, left.gender);
        EXPECT_EQ(right.noun_kind, left.noun_kind);
        EXPECT_EQ(right.pronoun_kind, left.pronoun_kind);
        EXPECT_EQ(right.adjective_degree, left.adjective_degree);
        EXPECT_EQ(right.packon, left.packon);
        EXPECT_EQ(right.enclitic, left.enclitic);
        EXPECT_EQ((*search)->addon_kind(right.id),
                  (*full)->addon_kind(left.id));
    }

    const auto full_rewrites = (*full)->rewrites();
    const auto search_rewrites = (*search)->rewrites();
    ASSERT_EQ(search_rewrites.size(), full_rewrites.size());
    for (std::size_t index = 0; index < full_rewrites.size(); ++index) {
        const auto &left = full_rewrites[index];
        const auto &right = search_rewrites[index];
        EXPECT_EQ(right.id, left.id);
        EXPECT_EQ(right.before, left.before);
        EXPECT_EQ(right.after, left.after);
        EXPECT_EQ(right.name, left.name);
        EXPECT_EQ(right.kind, left.kind);
        EXPECT_EQ(right.scope, left.scope);
        EXPECT_EQ(right.priority, left.priority);
        EXPECT_EQ(right.scan_reverse, left.scan_reverse);
        EXPECT_EQ(right.required_part, left.required_part);
        EXPECT_EQ(right.required_stem_key, left.required_stem_key);
        EXPECT_EQ(right.minimum_before, left.minimum_before);
        EXPECT_EQ(right.minimum_after, left.minimum_after);
        EXPECT_EQ(right.medieval, left.medieval);
        EXPECT_EQ(right.operation, left.operation);
        EXPECT_EQ(right.stage, left.stage);
        EXPECT_EQ(right.constraint, left.constraint);
    }
}

TEST(DatabaseTest, LoadsInflectionAndSparseStemQuantities) {
    constexpr auto first_declension_ablative_rule = RuleId{146U};
    constexpr auto first_declension_nominative_rule = RuleId{148U};
    constexpr std::uint32_t first_long_mal_entry = 26'263U;
    constexpr std::uint32_t last_long_mal_entry = 26'266U;
    constexpr std::uint32_t short_evil_entry = 26'267U;
    constexpr std::uint32_t short_bad_entry = 26'269U;

    auto database = Database::load_poc(test::read_database());
    ASSERT_TRUE(database) << database.error().message;

    const auto ablative =
        (*database)->inflection_quantity(first_declension_ablative_rule);
    EXPECT_EQ(ablative.known, 1U);
    EXPECT_EQ(ablative.long_vowel, 1U);
    const auto nominative =
        (*database)->inflection_quantity(first_declension_nominative_rule);
    EXPECT_EQ(nominative.known, 1U);
    EXPECT_EQ(nominative.long_vowel, 0U);

    for (const auto &reference : (*database)->lookup_stem("mal")) {
        const auto entry =
            (*database)->lexeme(reference.lexeme).dictionary_entry + 1U;
        const auto quantity = (*database)->stem_quantity(
            reference.lexeme, reference.lexical_slot);
        if (entry >= first_long_mal_entry && entry <= last_long_mal_entry) {
            EXPECT_EQ(quantity.known, 2U) << entry;
            EXPECT_EQ(quantity.long_vowel, 2U) << entry;
        } else if (entry == short_evil_entry || entry == short_bad_entry) {
            EXPECT_EQ(quantity.known, 2U) << entry;
            EXPECT_EQ(quantity.long_vowel, 0U) << entry;
        }
    }

    const auto expect_stem_quantity = [&](const std::string_view stem,
                                          const std::uint32_t entry,
                                          const std::uint32_t known,
                                          const std::uint32_t long_vowel) {
        const auto references = (*database)->lookup_stem(stem);
        const auto found = std::ranges::find_if(
            references, [&](const StemReference &reference) {
                return (*database)->lexeme(reference.lexeme).dictionary_entry +
                           1U ==
                       entry;
            });
        ASSERT_NE(found, references.end()) << entry;
        const auto quantity =
            (*database)->stem_quantity(found->lexeme, found->lexical_slot);
        EXPECT_EQ(quantity.known, known) << entry;
        EXPECT_EQ(quantity.long_vowel, long_vowel) << entry;
    };
    expect_stem_quantity("puell", 32'257U, 0b00010U, 0U);
    expect_stem_quantity("nomen", 27'969U, 0b00010U, 0b00010U);
    expect_stem_quantity("adhuc", 1'012U, 0b01001U, 0b01000U);
    expect_stem_quantity("defend", 16'105U, 0b00010U, 0b00010U);
    expect_stem_quantity("lev", 25'590U, 0b00010U, 0U);
    expect_stem_quantity("lev", 25'591U, 0b00010U, 0b00010U);
    expect_stem_quantity("popul", 30'955U, 0b01010U, 0U);
    expect_stem_quantity("popul", 30'957U, 0b01010U, 0b00010U);
}

TEST(DatabaseTest, DecodesAdjectiveDegreePayloads) {
    auto database = Database::load_poc(test::read_database());
    ASSERT_TRUE(database) << database.error().message;

    const auto has_lexical_degree = [&](const std::string_view stem,
                                        const Degree degree) {
        return std::ranges::any_of(
            (*database)->lookup_stem(stem),
            [&](const StemReference &reference) {
                const auto &lexeme = (*database)->lexeme(reference.lexeme);
                return lexeme.part_of_speech == PartOfSpeech::adjective &&
                       lexeme.adjective_degree == degree;
            });
    };
    EXPECT_TRUE(has_lexical_degree("pulcher", Degree::unknown));
    EXPECT_TRUE(has_lexical_degree("maxi", Degree::superlative));

    const auto has_positive_rule = std::ranges::any_of(
        (*database)->lookup_ending("us"), [&](const RuleId id) {
            const auto &rule = (*database)->rule(id);
            return rule.part_of_speech == PartOfSpeech::adjective &&
                   rule.adjective_degree == Degree::positive;
        });
    EXPECT_TRUE(has_positive_rule);
}

TEST(DatabaseTest, DecodesRemainingSemanticPayloads) {
    auto database = Database::load_poc(test::read_database());
    ASSERT_TRUE(database) << database.error().message;

    const auto has_lexeme = [&](const std::string_view stem,
                                const auto predicate) {
        return std::ranges::any_of(
            (*database)->lookup_stem(stem),
            [&](const StemReference &reference) {
                return predicate((*database)->lexeme(reference.lexeme));
            });
    };
    EXPECT_TRUE(has_lexeme("un", [](const LexemeRecord &lexeme) {
        return lexeme.part_of_speech == PartOfSpeech::numeral &&
               lexeme.numeral_value == 1U;
    }));
    EXPECT_TRUE(has_lexeme("bene", [](const LexemeRecord &lexeme) {
        return lexeme.part_of_speech == PartOfSpeech::adverb &&
               lexeme.adverb_degree == Degree::unknown;
    }));
    EXPECT_TRUE(has_lexeme("am", [](const LexemeRecord &lexeme) {
        return lexeme.part_of_speech == PartOfSpeech::verb &&
               lexeme.declension == 1U;
    }));
    EXPECT_TRUE(has_lexeme("cum", [](const LexemeRecord &lexeme) {
        return lexeme.part_of_speech == PartOfSpeech::preposition &&
               lexeme.governs == GrammaticalCase::ablative;
    }));

    EXPECT_TRUE(std::ranges::any_of(
        (*database)->lookup_ending("o"), [&](const RuleId id) {
            const auto &rule = (*database)->rule(id);
            return rule.part_of_speech == PartOfSpeech::verb &&
                   rule.tense == Tense::present &&
                   rule.voice == Voice::active &&
                   rule.mood == Mood::indicative &&
                   rule.person == Person::first;
        }));
}

TEST(DatabaseTest, ClosedSemanticDomainsHaveTypedCanonicalNames) {
    static_assert(std::is_enum_v<NounKind>);
    static_assert(std::is_enum_v<Age>);
    static_assert(std::is_enum_v<SubjectArea>);
    static_assert(std::is_enum_v<Geography>);
    static_assert(std::is_enum_v<LexicalFrequency>);
    static_assert(std::is_enum_v<RuleFrequency>);
    static_assert(std::is_enum_v<Source>);
    static_assert(std::is_enum_v<Person>);

    const auto expect_closed_domain = []<class Enum>(const Enum last,
                                                     const auto name) {
        EXPECT_TRUE(name(static_cast<Enum>(0U)).empty());
        for (std::uint8_t ordinal = 1U; ordinal <= std::to_underlying(last);
             ++ordinal) {
            EXPECT_FALSE(name(static_cast<Enum>(ordinal)).empty())
                << static_cast<unsigned>(ordinal);
        }
        // Intentionally probes the presentation boundary with an invalid wire
        // code; fixed-underlying enums can represent the full uint8_t range.
        // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange)
        const auto invalid = static_cast<Enum>(std::to_underlying(last) + 1U);
        EXPECT_TRUE(name(invalid).empty());
    };

    expect_closed_domain(NounKind::place, noun_kind_name);
    expect_closed_domain(Age::modern, age_name);
    expect_closed_domain(SubjectArea::mythology, subject_name);
    expect_closed_domain(Geography::eastern_europe, geography_name);
    expect_closed_domain(LexicalFrequency::pliny, lexical_frequency_name);
    expect_closed_domain(RuleFrequency::reserved_n, rule_frequency_name);
    expect_closed_domain(Source::user_submitted, source_name);
    expect_closed_domain(GrammaticalCase::accusative, case_name);
    expect_closed_domain(Gender::common, gender_name);
    expect_closed_domain(GrammaticalNumber::plural, number_name);
    expect_closed_domain(Degree::superlative, degree_name);
    expect_closed_domain(PronounKind::adjectival, pronoun_kind_name);
    expect_closed_domain(NumeralType::adverbial, numeral_type_name);
    expect_closed_domain(Tense::future_perfect, tense_name);
    expect_closed_domain(Voice::passive, voice_name);
    expect_closed_domain(Mood::participle, mood_name);
    expect_closed_domain(VerbKind::perfect_definite, verb_kind_name);

    EXPECT_EQ(governed_case(VerbKind::governs_genitive),
              GrammaticalCase::genitive);
    EXPECT_EQ(governed_case(VerbKind::governs_dative), GrammaticalCase::dative);
    EXPECT_EQ(governed_case(VerbKind::governs_ablative),
              GrammaticalCase::ablative);
    EXPECT_FALSE(governed_case(VerbKind::transitive).has_value());
}

TEST(DatabaseTest, AuditsEveryCategoricalCodeInTheDataset) {
    const auto &database = test::engine().database();
    std::array<bool, std::to_underlying(VerbKind::perfect_definite) + 1U>
        verb_kinds{};
    std::array<bool, std::to_underlying(GrammaticalCase::accusative) + 1U>
        governed_cases{};

    for (const auto &lexeme : database.lexemes()) {
        EXPECT_LE(std::to_underlying(lexeme.part_of_speech),
                  std::to_underlying(PartOfSpeech::interjection));
        EXPECT_LE(std::to_underlying(lexeme.age),
                  std::to_underlying(Age::modern));
        EXPECT_LE(std::to_underlying(lexeme.subject),
                  std::to_underlying(SubjectArea::mythology));
        EXPECT_LE(std::to_underlying(lexeme.geography),
                  std::to_underlying(Geography::eastern_europe));
        EXPECT_LE(std::to_underlying(lexeme.frequency),
                  std::to_underlying(LexicalFrequency::pliny));
        EXPECT_LE(std::to_underlying(lexeme.source),
                  std::to_underlying(Source::user_submitted));
        if (lexeme.part_of_speech == PartOfSpeech::verb) {
            verb_kinds.at(std::to_underlying(lexeme.verb_kind)) = true;
        }
        if (lexeme.part_of_speech == PartOfSpeech::preposition) {
            governed_cases.at(std::to_underlying(lexeme.governs)) = true;
        }
    }

    // This snapshot contains every legacy Verb_Kind except GEN.  The absence
    // of a real GEN entry is a dataset fact, not a reason to synthesize one.
    EXPECT_TRUE(verb_kinds.at(std::to_underlying(VerbKind::unknown)));
    EXPECT_TRUE(verb_kinds.at(std::to_underlying(VerbKind::to_be)));
    EXPECT_TRUE(verb_kinds.at(std::to_underlying(VerbKind::compound_of_to_be)));
    EXPECT_FALSE(verb_kinds.at(std::to_underlying(VerbKind::governs_genitive)));
    for (const auto kind :
         {VerbKind::governs_dative, VerbKind::governs_ablative,
          VerbKind::transitive, VerbKind::intransitive, VerbKind::impersonal,
          VerbKind::deponent, VerbKind::semideponent,
          VerbKind::perfect_definite}) {
        EXPECT_TRUE(verb_kinds.at(std::to_underlying(kind)))
            << verb_kind_name(kind);
    }

    EXPECT_TRUE(
        governed_cases.at(std::to_underlying(GrammaticalCase::genitive)));
    EXPECT_TRUE(
        governed_cases.at(std::to_underlying(GrammaticalCase::ablative)));
    EXPECT_TRUE(
        governed_cases.at(std::to_underlying(GrammaticalCase::accusative)));

    for (const auto &rule : database.rules()) {
        EXPECT_LE(std::to_underlying(rule.part_of_speech),
                  std::to_underlying(PartOfSpeech::interjection));
        EXPECT_LE(std::to_underlying(rule.grammatical_case),
                  std::to_underlying(GrammaticalCase::accusative));
        EXPECT_LE(std::to_underlying(rule.number),
                  std::to_underlying(GrammaticalNumber::plural));
        EXPECT_LE(std::to_underlying(rule.gender),
                  std::to_underlying(Gender::common));
        EXPECT_LE(std::to_underlying(rule.tense),
                  std::to_underlying(Tense::future_perfect));
        EXPECT_LE(std::to_underlying(rule.voice),
                  std::to_underlying(Voice::passive));
        EXPECT_LE(std::to_underlying(rule.mood),
                  std::to_underlying(Mood::participle));
        EXPECT_LE(std::to_underlying(rule.person),
                  std::to_underlying(Person::third));
        EXPECT_LE(std::to_underlying(rule.age),
                  std::to_underlying(Age::modern));
        EXPECT_LE(std::to_underlying(rule.frequency),
                  std::to_underlying(RuleFrequency::reserved_n));
    }

    for (const auto &suffix : database.suffixes()) {
        EXPECT_EQ(database.addon_kind(suffix.id), AddonKind::suffix);
    }
    for (const auto &prefix : database.prefixes()) {
        const auto kind = database.addon_kind(prefix.id);
        EXPECT_TRUE(kind == AddonKind::prefix || kind == AddonKind::tickon);
    }
    for (const auto &tackon : database.tackons()) {
        const auto kind = database.addon_kind(tackon.id);
        EXPECT_EQ(kind == AddonKind::packon, tackon.packon);
        EXPECT_FALSE(tackon.packon && tackon.enclitic);
    }
}

TEST(DatabaseTest, LoadsAndIndexesUniqueAnalyses) {
    auto database = Database::load_poc(test::read_database());
    ASSERT_TRUE(database) << database.error().message;

    const auto homographs = (*database)->lookup_unique("eadem");
    ASSERT_EQ(homographs.size(), 3U);
    EXPECT_TRUE(
        std::ranges::all_of(homographs, [&](const UniqueReference &reference) {
            const auto &lexeme = (*database)->lexeme(reference.lexeme);
            return lexeme.dictionary == DictionaryKind::unique &&
                   lexeme.part_of_speech == PartOfSpeech::pronoun &&
                   std::holds_alternative<PronounMorphology>(
                       reference.morphology);
        }));

    // The index uses the same i/j and u/v equivalence as Ada, so alternate
    // orthography reaches the stored form without duplicating keys.
    EXPECT_EQ((*database)->lookup_unique("mauis").size(), 1U);
    EXPECT_TRUE((*database)->lookup_unique("zzzzzz").empty());
}

TEST(DatabaseTest, LoadsTypedRewriteMicroRules) {
    const auto rules = test::engine().database().rewrites();
    ASSERT_EQ(rules.size(), 170U);
    EXPECT_EQ(
        std::ranges::count(rules, RewriteKind::syncope, &RewriteRule::kind),
        11U);
    EXPECT_EQ(std::ranges::count(rules, RewriteKind::orthographic,
                                 &RewriteRule::kind),
              159U);
    const auto &first = rules.front();
    EXPECT_EQ(first.id, RewriteId{0U});
    EXPECT_EQ(first.kind, RewriteKind::syncope);
    EXPECT_EQ(first.scope, RewriteScope::internal);
    EXPECT_EQ(first.priority, 0U);
    EXPECT_TRUE(first.scan_reverse);
    EXPECT_EQ(first.operation, RewriteOperation::literal);
    EXPECT_EQ(first.stage, RewriteStage::main);
    EXPECT_EQ(first.constraint, RewriteConstraint::none);
    EXPECT_EQ(first.required_part, PartOfSpeech::verb);
    EXPECT_EQ(first.required_stem_key, 3U);
    EXPECT_EQ(test::engine().database().rewrite_string(first.before), "ii");
    EXPECT_EQ(test::engine().database().rewrite_string(first.after), "ivi");
    EXPECT_EQ(test::engine().database().rewrite_string(first.name),
              "perfect-ivi-uncontracted");

    const auto slur = std::ranges::find_if(rules, [&](const RewriteRule &rule) {
        return test::engine().database().rewrite_string(rule.name) ==
               "orth-slur-ob";
    });
    ASSERT_NE(slur, rules.end());
    EXPECT_EQ(slur->operation, RewriteOperation::slur);
    EXPECT_EQ(slur->stage, RewriteStage::early);
}

TEST(DatabaseTest, LoadsAndIndexesSuffixRules) {
    auto database = Database::load_poc(test::read_database());
    ASSERT_TRUE(database) << database.error().message;

    const auto suffixes = (*database)->lookup_suffix("icul");
    ASSERT_EQ(suffixes.size(), 3U);
    EXPECT_LE((*database)->maximum_suffix_size(), 8U);
    EXPECT_TRUE(std::ranges::all_of(suffixes, [&](const AddonId id) {
        const auto &suffix = (*database)->suffix(id);
        return suffix.root == PartOfSpeech::noun &&
               suffix.target == PartOfSpeech::noun &&
               (*database)->suffix_string(suffix.fix) == "icul";
    }));
}

TEST(DatabaseTest, LoadsAndIndexesPrefixRules) {
    auto database = Database::load_poc(test::read_database());
    ASSERT_TRUE(database) << database.error().message;

    const auto prefixes = (*database)->lookup_prefix("archi");
    ASSERT_FALSE(prefixes.empty());
    EXPECT_LE((*database)->maximum_prefix_size(), 18U);
    EXPECT_TRUE(std::ranges::all_of(prefixes, [&](const AddonId id) {
        const auto &prefix = (*database)->prefix(id);
        return (*database)->addon_kind(id) == AddonKind::prefix &&
               prefix.root == PartOfSpeech::unknown &&
               prefix.target == PartOfSpeech::unknown &&
               (*database)->prefix_string(prefix.fix) == "archi";
    }));

    const auto assimilated = (*database)->lookup_prefix("ap");
    ASSERT_FALSE(assimilated.empty());
    EXPECT_TRUE(std::ranges::any_of(assimilated, [&](const AddonId id) {
        return (*database)->prefix(id).connector == 'p';
    }));
}

TEST(DatabaseTest, SeparatesTickonsTackonsAndPackons) {
    auto database = Database::load_poc(test::read_database());
    ASSERT_TRUE(database) << database.error().message;

    const auto tickons = (*database)->lookup_tickon("ec");
    ASSERT_FALSE(tickons.empty());
    EXPECT_TRUE(std::ranges::all_of(tickons, [&](const AddonId id) {
        return (*database)->addon_kind(id) == AddonKind::tickon &&
               (*database)->prefix(id).root == PartOfSpeech::pack;
    }));

    const auto tackons = (*database)->lookup_tackon("que");
    ASSERT_FALSE(tackons.empty());
    EXPECT_TRUE(std::ranges::all_of(tackons, [&](const AddonId id) {
        const auto &tackon = (*database)->tackon(id);
        return (*database)->addon_kind(id) == AddonKind::tackon &&
               tackon.enclitic && !tackon.packon;
    }));

    const auto packons = (*database)->lookup_packon("dam");
    ASSERT_FALSE(packons.empty());
    EXPECT_TRUE(std::ranges::all_of(packons, [&](const AddonId id) {
        const auto &packon = (*database)->tackon(id);
        return (*database)->addon_kind(id) == AddonKind::packon &&
               packon.base == PartOfSpeech::pack && packon.packon;
    }));
}

TEST(DatabaseTest, RejectsWrongMagic) {
    auto bytes = test::read_database();
    bytes[0] = std::byte{'X'};
    const auto database = Database::load_poc(std::move(bytes));
    ASSERT_FALSE(database);
    EXPECT_EQ(database.error().code, "invalid-magic");
}

TEST(DatabaseTest, RejectsUnsupportedProfile) {
    auto bytes = test::read_database();
    bytes[20] = std::byte{3};
    const auto database = Database::load_poc(std::move(bytes));
    ASSERT_FALSE(database);
    EXPECT_EQ(database.error().code, "unsupported-profile");
}

TEST(DatabaseTest, RejectsUnsupportedVersionAndHeaderSize) {
    for (const auto minor : {std::uint16_t{5U}, std::uint16_t{9U}}) {
        SCOPED_TRACE(minor);
        auto bytes = test::read_database();
        write_u16_le(bytes, test_header_minor_offset, minor);
        const auto database = Database::load_poc(std::move(bytes));
        ASSERT_FALSE(database);
        EXPECT_EQ(database.error().code, "unsupported-version");
    }

    auto wrong_major = test::read_database();
    write_u16_le(wrong_major, test_header_major_offset, 2U);
    const auto major_result = Database::load_poc(std::move(wrong_major));
    ASSERT_FALSE(major_result);
    EXPECT_EQ(major_result.error().code, "unsupported-version");

    auto wrong_header_size = test::read_database();
    write_u32_le(wrong_header_size, test_header_size_offset, 39U);
    const auto header_result = Database::load_poc(std::move(wrong_header_size));
    ASSERT_FALSE(header_result);
    EXPECT_EQ(header_result.error().code, "unsupported-version");
}

TEST(DatabaseTest, RejectsUnsafeSectionCountsAndTypes) {
    for (const auto count : {std::uint32_t{0U}, std::uint32_t{65U}}) {
        SCOPED_TRACE(count);
        auto bytes = test::read_database();
        write_u32_le(bytes, test_header_section_count_offset, count);
        const auto database = Database::load_poc(std::move(bytes));
        ASSERT_FALSE(database);
        EXPECT_EQ(database.error().code, "invalid-directory");
    }

    for (const auto type : {std::uint32_t{0U}, std::uint32_t{24U}}) {
        SCOPED_TRACE(type);
        auto bytes = test::read_database();
        write_u32_le(bytes, test_directory_offset, type);
        const auto database = Database::load_poc(std::move(bytes));
        ASSERT_FALSE(database);
        EXPECT_EQ(database.error().code, "unknown-section");
    }
}

TEST(DatabaseTest, RejectsUnsupportedSectionShape) {
    auto bytes = test::read_database();
    write_u32_le(bytes, test_directory_flags_offset, 1U);
    const auto database = Database::load_poc(std::move(bytes));
    ASSERT_FALSE(database);
    EXPECT_EQ(database.error().code, "unsupported-section-layout");
}

TEST(DatabaseTest, RejectsTruncation) {
    auto bytes = test::read_database();
    bytes.resize(20U);
    const auto database = Database::load_poc(std::move(bytes));
    ASSERT_FALSE(database);
    EXPECT_EQ(database.error().code, "truncated-database");
}

TEST(DatabaseTest, RejectsPayloadCorruption) {
    auto bytes = test::read_database();
    bytes.back() ^= std::byte{1};
    const auto database = Database::load_poc(std::move(bytes));
    ASSERT_FALSE(database);
    EXPECT_EQ(database.error().code, "checksum-mismatch");
}

} // namespace words
