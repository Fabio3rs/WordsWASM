// Quick proof of concept for the compact layout described in
// docs/auditoria-binarios-e-formato-compacto.md.
//
// This fixture/release generator reads the concrete GNAT/x86-64 legacy files
// in this repository and writes a portable, explicitly little-endian image.
// Version 1.9 retains the vowel
// quantity masks and typed PACKON selector from 1.8 and adds sparse, packed
// morphological notices. Version 1.10 persists the exact canonical order of
// stem references so production loaders do not rebuild and sort that index.

#include <algorithm>
#include <array>
#include <bit>
#include <charconv>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <iostream>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "words/detail/wwdb_schema.hpp"
#include "words/model.hpp"

namespace {

using Bytes = std::vector<std::byte>;
namespace wwdb = words::detail::wwdb;
using PackingProfile = wwdb::Profile;
using SectionType = wwdb::SectionType;

// Offsets in the GNAT/x86-64 source files. These are source layouts, not WWDB
// offsets; keep their bounds checked against the source record sizes.
constexpr std::size_t dictionary_record_size = 180U;
constexpr std::size_t dictionary_stem_width = wwdb::maximum_stem_size;
constexpr std::size_t dictionary_part_of_speech_offset = 72U;
constexpr std::size_t dictionary_paradigm_offset = 76U;
constexpr std::size_t dictionary_variant_offset = 80U;
constexpr std::size_t dictionary_class_attribute_offset = 84U;
constexpr std::size_t dictionary_second_attribute_offset = 85U;
constexpr std::size_t dictionary_numeric_value_offset = 88U;
constexpr std::size_t dictionary_age_offset = 92U;
constexpr std::size_t dictionary_area_offset = 93U;
constexpr std::size_t dictionary_geography_offset = 94U;
constexpr std::size_t dictionary_frequency_offset = 95U;
constexpr std::size_t dictionary_source_offset = 96U;
constexpr std::size_t dictionary_meaning_offset = 97U;
constexpr std::size_t legacy_meaning_width = 80U;
constexpr std::size_t stem_record_size = 56U;
constexpr std::size_t stem_key_offset = 40U;
constexpr std::size_t stem_dictionary_entry_offset = 48U;
constexpr std::size_t inflection_record_size = 40U;
constexpr std::size_t inflection_part_of_speech_offset = 0U;
constexpr std::size_t inflection_paradigm_offset = 4U;
constexpr std::size_t inflection_adverb_case_offset = 4U;
constexpr std::size_t inflection_variant_offset = 8U;
constexpr std::size_t inflection_morphology_offset = 12U;
constexpr std::size_t inflection_stem_key_offset = 20U;
constexpr std::size_t inflection_ending_size_offset = 24U;
constexpr std::size_t inflection_ending_offset = 28U;
constexpr std::size_t inflection_age_offset = 36U;
constexpr std::size_t inflection_frequency_offset = 37U;
constexpr std::size_t inflections_per_section = 570U;
constexpr std::size_t inflection_section_count = wwdb::inflection_section_count;
constexpr std::uint8_t maximum_paradigm_component = 9U;
constexpr std::uint16_t maximum_imported_numeral_value = 1000U;
constexpr std::uint8_t maximum_legacy_source =
    std::to_underlying(words::Source::user_submitted);
constexpr std::uint8_t imported_lexeme_source =
    std::to_underlying(words::Source::other_dictionaries);
constexpr std::uint8_t maximum_lexical_age =
    std::to_underlying(words::Age::modern);
constexpr std::uint8_t maximum_subject_area =
    std::to_underlying(words::SubjectArea::mythology);
constexpr std::uint8_t maximum_geography =
    std::to_underlying(words::Geography::eastern_europe);
constexpr std::uint8_t maximum_lexical_frequency =
    std::to_underlying(words::LexicalFrequency::pliny);
constexpr std::uint8_t maximum_rule_frequency =
    std::to_underlying(words::RuleFrequency::reserved_n);
constexpr std::uint8_t maximum_noun_gender =
    std::to_underlying(words::Gender::common);
constexpr std::uint8_t maximum_numeral_type =
    std::to_underlying(words::NumeralType::adverbial);
constexpr std::uint8_t maximum_encoded_noun_kind =
    wwdb::low_mask<std::uint8_t>(4U);
constexpr std::uint8_t maximum_simple_class =
    wwdb::low_mask<std::uint8_t>(wwdb::simple_class_used_bits);
constexpr std::uint8_t maximum_adjective_degree =
    std::to_underlying(words::Degree::superlative);
constexpr std::uint16_t maximum_numeral_value = wwdb::numeral_value_mask;
constexpr std::uint32_t legacy_simple_lexeme_stride = 19U;
constexpr std::uint32_t legacy_simple_inflection_stride = 8U;
constexpr std::size_t maximum_stem_key = wwdb::lexical_slot_count;
constexpr std::uint8_t maximum_rewrite_context_size = wwdb::nibble_mask;
constexpr std::size_t inflection_participle_mood_offset =
    inflection_morphology_offset + 5U;
static_assert(inflection_participle_mood_offset < inflection_stem_key_offset);
constexpr int required_argument_count = 3;
constexpr int maximum_argument_count = 5;
namespace suffix_fields {
constexpr std::size_t minimum_target_count = 5U;
constexpr std::size_t noun_or_numeral_count = 8U;
constexpr std::size_t adjective_or_verb_count = 7U;
constexpr std::size_t attribute = 5U;
constexpr std::size_t second_attribute = 6U;
constexpr std::size_t key_after_two_attributes = 7U;
constexpr std::size_t key_after_one_attribute = 6U;
} // namespace suffix_fields
constexpr std::size_t tackon_noun_target_count = 5U;
namespace unique_fields {
constexpr std::size_t nominal_count = 12U;
constexpr std::size_t verb_count = 14U;
constexpr std::size_t nominal_attribute = 6U;
constexpr std::size_t gender = 5U;
constexpr std::size_t nominal_translation = 7U;
constexpr std::size_t verb_number = 7U;
constexpr std::size_t verb_kind = 8U;
constexpr std::size_t verb_person = 6U;
constexpr std::size_t verb_translation = 9U;
} // namespace unique_fields
namespace rewrite_fields {
constexpr std::size_t count = 15U;
constexpr std::size_t direction = 5U;
constexpr std::size_t before = 6U;
constexpr std::size_t after = 7U;
constexpr std::size_t required_part = 8U;
constexpr std::size_t required_stem_key = 9U;
constexpr std::size_t minimum_before = 10U;
constexpr std::size_t minimum_after = 11U;
constexpr std::size_t constraint = 12U;
constexpr std::size_t era = 13U;
constexpr std::size_t name = 14U;
} // namespace rewrite_fields
namespace quantity_fields {
constexpr std::size_t stem_count = 5U;
constexpr std::size_t suffix_count = 11U;
constexpr std::size_t suffix_target_part = 5U;
constexpr std::size_t suffix_target_key = 6U;
constexpr std::size_t suffix_root_declension = 7U;
constexpr std::size_t suffix_root_variant = 8U;
constexpr std::size_t suffix_known = 9U;
constexpr std::size_t suffix_long = 10U;
} // namespace quantity_fields
namespace policy_fields {
constexpr std::size_t count = 12U;
constexpr std::size_t terminator = 11U;
constexpr std::size_t target_part = 5U;
constexpr std::size_t target_key = 6U;
constexpr std::size_t target_degree = 7U;
constexpr std::size_t connector = 8U;
constexpr std::size_t root_declension = 9U;
constexpr std::size_t root_variant = 10U;
} // namespace policy_fields
constexpr std::uint8_t pos_noun = std::to_underlying(words::PartOfSpeech::noun);
constexpr std::uint8_t pos_pronoun =
    std::to_underlying(words::PartOfSpeech::pronoun);
constexpr std::uint8_t pos_pack = std::to_underlying(words::PartOfSpeech::pack);
constexpr std::uint8_t pos_adjective =
    std::to_underlying(words::PartOfSpeech::adjective);
constexpr std::uint8_t pos_numeral =
    std::to_underlying(words::PartOfSpeech::numeral);
constexpr std::uint8_t pos_adverb =
    std::to_underlying(words::PartOfSpeech::adverb);
constexpr std::uint8_t pos_verb = std::to_underlying(words::PartOfSpeech::verb);
constexpr std::uint8_t pos_participle =
    std::to_underlying(words::PartOfSpeech::participle);
constexpr std::uint8_t pos_supine =
    std::to_underlying(words::PartOfSpeech::supine);
constexpr std::uint8_t pos_preposition =
    std::to_underlying(words::PartOfSpeech::preposition);
constexpr std::uint8_t pos_conjunction =
    std::to_underlying(words::PartOfSpeech::conjunction);
constexpr std::uint8_t pos_interjection =
    std::to_underlying(words::PartOfSpeech::interjection);
static_assert(dictionary_stem_width * wwdb::lexical_slot_count ==
              dictionary_part_of_speech_offset);
static_assert(dictionary_meaning_offset + legacy_meaning_width <=
              dictionary_record_size);
static_assert(dictionary_numeric_value_offset + wwdb::u32_size <=
              dictionary_age_offset);
static_assert(stem_dictionary_entry_offset + wwdb::u64_size ==
              stem_record_size);
static_assert(stem_key_offset + wwdb::u32_size <= stem_dictionary_entry_offset);
static_assert(inflection_frequency_offset < inflection_record_size);
static_assert(inflection_ending_offset + wwdb::maximum_ending_size <=
              inflection_age_offset);
static_assert(inflection_stem_key_offset + wwdb::u32_size ==
              inflection_ending_size_offset);
static_assert(inflection_ending_size_offset + wwdb::u32_size ==
              inflection_ending_offset);
constexpr std::uint8_t legacy_pack_part_of_speech = 3U;
constexpr auto legacy_verb_part_of_speech =
    static_cast<std::uint8_t>(std::to_underlying(words::PartOfSpeech::verb));
constexpr auto legacy_semideponent_kind = static_cast<std::uint8_t>(
    std::to_underlying(words::VerbKind::semideponent));
constexpr auto semideponent_passive_present_trigger =
    static_cast<std::uint8_t>(std::to_underlying(
        words::WhitakerTrimReason::semideponent_passive_present_system));
constexpr std::string_view ada_comment_marker{"--"};
constexpr std::uint16_t maximum_encoded_packon_plus_one = 511U;
constexpr std::string_view morphological_notices_file{
    "MORPHOLOGICAL_NOTICES.LAT"};
constexpr std::array<std::string_view, 6U> whitaker_trim_reason_names{
    "UNSUPPORTED_SHORT_IMPERATIVE",        "INVALID_IMPERATIVE_PERSON",
    "IMPERSONAL_NON_THIRD_PERSON",         "DEPONENT_ACTIVE_FORM",
    "SEMIDEPONENT_PASSIVE_PRESENT_SYSTEM", "SEMIDEPONENT_ACTIVE_PERFECT_SYSTEM",
};
constexpr std::array<std::string_view, 3U> morphological_notice_names{
    "RELATED_PASSIVE_USAGE_ATTESTED",
    "SOURCE_DISAGREEMENT",
    "MANUAL_REVIEW_RECOMMENDED",
};
static_assert(whitaker_trim_reason_names.size() ==
              words::whitaker_trim_reason_count);
static_assert(morphological_notice_names.size() ==
              words::morphological_notice_count);

struct Section {
    SectionType type;
    std::uint32_t flags{};
    std::uint32_t count{};
    std::uint32_t stride{};
    Bytes data;
};

struct SuffixSource final {
    std::uint16_t addon_id{};
    std::string fix;
    std::string meaning;
    std::uint8_t connect{};
    std::uint8_t root{};
    std::uint8_t root_key{};
    std::uint8_t target{};
    std::uint8_t target_key{};
    std::uint8_t paradigm{};
    std::uint8_t attribute_0{};
    std::uint8_t attribute_1{};
    std::uint8_t numeric_value{};
};

struct PrefixSource final {
    std::uint16_t addon_id{};
    std::string fix;
    std::string meaning;
    std::uint8_t connect{};
    std::uint8_t root{};
    std::uint8_t target{};
};

struct TackonSource final {
    std::uint16_t addon_id{};
    std::string fix;
    std::string meaning;
    std::uint8_t base{};
    std::uint8_t paradigm{};
    std::uint8_t attribute_0{};
    std::uint8_t attribute_1{};
    bool packon{};
    bool enclitic{};
};

struct AddonSources final {
    std::vector<PrefixSource> prefixes;
    std::vector<SuffixSource> suffixes;
    std::vector<TackonSource> tackons;
};

struct UniqueSource final {
    std::string surface;
    std::string meaning;
    std::uint8_t part_of_speech{};
    std::uint8_t paradigm{};
    std::uint16_t morphology{};
    std::uint32_t translation{};
};

struct RewriteSource final {
    std::string before;
    std::string after;
    std::string name;
    std::string meaning;
    std::uint8_t kind{};
    std::uint8_t scope{};
    std::uint8_t priority{};
    bool scan_reverse{};
    std::uint8_t required_part{};
    std::uint8_t required_stem_key{};
    std::uint8_t minimum_before{};
    std::uint8_t minimum_after{};
    bool medieval{};
    std::uint8_t operation{};
    std::uint8_t stage{};
    std::uint8_t constraint{};
};

struct InflectionQuantitySource final {
    std::uint16_t rule_id{};
    std::uint32_t known{};
    std::uint32_t long_vowel{};
};

struct StemQuantitySource final {
    std::uint16_t dictionary_entry{};
    std::uint8_t lexical_slot{};
    std::uint32_t known{};
    std::uint32_t long_vowel{};
};

struct SuffixAttributeSource final {
    std::uint16_t addon_id{};
    std::string fix;
    std::uint8_t root{};
    std::uint8_t root_key{};
    std::uint8_t target{};
    std::uint8_t target_key{};
    std::uint8_t root_declension{};
    std::uint8_t root_variant{};
    std::uint32_t known{};
    std::uint32_t long_vowel{};
};

struct SuffixPolicySource final {
    std::uint16_t addon_id{};
    std::string fix;
    std::uint8_t root{};
    std::uint8_t root_key{};
    std::uint8_t target{};
    std::uint8_t target_key{};
    std::uint8_t target_degree{};
    char connector{};
    std::uint8_t root_declension{};
    std::uint8_t root_variant{};
};

struct QuantitySources final {
    std::vector<InflectionQuantitySource> inflections;
    std::vector<StemQuantitySource> stems;
    std::vector<SuffixAttributeSource> suffixes;
};

struct MorphologicalNoticeSource final {
    std::uint16_t dictionary_entry{};
    std::uint8_t trigger{};
    std::uint8_t notice{};
    auto operator<=>(const MorphologicalNoticeSource &) const = default;
};

struct CompiledLexeme final {
    std::string decision_id;
    std::array<std::string, 4> stems;
    std::string meaning;
    std::uint8_t part_of_speech{};
    std::uint8_t paradigm{};
    std::uint16_t class_payload{};
    std::uint16_t numeric_value{};
    std::uint32_t translation{};
};

std::uint8_t pack_paradigm(std::uint32_t which, std::uint32_t variant);

[[noreturn]] void fail(const std::string &message) {
    throw std::runtime_error(message);
}

std::string_view trim(std::string_view value);

Bytes read_file(const std::filesystem::path &path) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input) {
        fail("cannot open input: " + path.string());
    }

    const auto end = input.tellg();
    if (end < 0) {
        fail("cannot determine input size: " + path.string());
    }

    Bytes result(static_cast<std::size_t>(end));
    input.seekg(0);
    input.read(reinterpret_cast<char *>(result.data()),
               static_cast<std::streamsize>(result.size()));
    if (!input) {
        fail("cannot read input: " + path.string());
    }
    return result;
}

std::uint32_t json_u32(const nlohmann::json &record,
                       const std::string_view field,
                       const std::uint32_t maximum,
                       const std::string_view context) {
    const auto found = record.find(field);
    if (found == record.end() || !found->is_number_unsigned()) {
        fail(std::string(context) + ": " + std::string(field) +
             " must be an unsigned integer");
    }
    const auto value = found->get<std::uint64_t>();
    if (value > maximum) {
        fail(std::string(context) + ": " + std::string(field) +
             " exceeds the microformat range");
    }
    return static_cast<std::uint32_t>(value);
}

std::vector<CompiledLexeme>
read_compiled_lexemes(const std::filesystem::path &path) {
    if (!std::filesystem::exists(path)) {
        return {};
    }
    std::ifstream input(path);
    if (!input) {
        fail("cannot open input: " + path.string());
    }

    constexpr std::string_view expected_schema{
        "whitakers-words.compiled-lexeme.v1"};
    std::vector<CompiledLexeme> result;
    std::unordered_map<std::string, std::size_t> decisions;
    std::string line;
    std::size_t line_number = 0;
    while (std::getline(input, line)) {
        ++line_number;
        if (trim(line).empty()) {
            continue;
        }
        const auto context = path.string() + ':' + std::to_string(line_number);
        nlohmann::json record;
        try {
            record = nlohmann::json::parse(line);
        } catch (const nlohmann::json::exception &error) {
            fail(context + ": invalid JSON: " + error.what());
        }
        constexpr std::array<std::string_view, 9> fields{
            "schema",        "decision_id",    "stems",
            "meaning",       "part_of_speech", "paradigm",
            "class_payload", "numeric_value",  "translation",
        };
        if (!record.is_object() || record.size() != fields.size() ||
            !std::ranges::all_of(fields, [&](const std::string_view field) {
                return record.contains(field);
            })) {
            fail(context + ": compiled lexeme has an unexpected shape");
        }
        if (!record["schema"].is_string() ||
            record["schema"].get<std::string>() != expected_schema ||
            !record["decision_id"].is_string() ||
            !record["meaning"].is_string()) {
            fail(context + ": invalid compiled lexeme identity/text fields");
        }

        CompiledLexeme lexeme;
        lexeme.decision_id = record["decision_id"].get<std::string>();
        lexeme.meaning = record["meaning"].get<std::string>();
        if (lexeme.decision_id.empty() || lexeme.meaning.empty() ||
            lexeme.meaning.size() > std::numeric_limits<std::uint8_t>::max()) {
            fail(context +
                 ": empty ID/meaning or meaning exceeds 255 UTF-8 bytes");
        }
        if (!decisions.emplace(lexeme.decision_id, line_number).second) {
            fail(context + ": duplicate decision_id " + lexeme.decision_id);
        }
        if (!record["stems"].is_array() ||
            record["stems"].size() != wwdb::lexical_slot_count) {
            fail(context + ": stems must contain exactly four strings");
        }
        bool has_stem = false;
        for (std::size_t slot = 0; slot < lexeme.stems.size(); ++slot) {
            const auto &source = record["stems"][slot];
            if (!source.is_string()) {
                fail(context + ": stems must contain only strings");
            }
            lexeme.stems[slot] = source.get<std::string>();
            const auto valid = std::ranges::all_of(
                lexeme.stems[slot], [](const char character) {
                    return character >= 'a' && character <= 'z';
                });
            if (lexeme.stems[slot].size() > dictionary_stem_width || !valid) {
                fail(context +
                     ": stem must contain at most 18 lowercase ASCII letters");
            }
            has_stem = has_stem || !lexeme.stems[slot].empty();
        }
        if (!has_stem) {
            fail(context + ": compiled lexeme has no radical");
        }

        lexeme.part_of_speech = static_cast<std::uint8_t>(
            json_u32(record, "part_of_speech",
                     std::to_underlying(words::PartOfSpeech::suffix), context));
        lexeme.paradigm = static_cast<std::uint8_t>(
            json_u32(record, "paradigm",
                     std::numeric_limits<std::uint8_t>::max(), context));
        lexeme.class_payload = static_cast<std::uint16_t>(json_u32(
            record, "class_payload", wwdb::lexeme_class_payload_mask, context));
        lexeme.numeric_value = static_cast<std::uint16_t>(json_u32(
            record, "numeric_value", maximum_imported_numeral_value, context));
        lexeme.translation =
            json_u32(record, "translation", wwdb::translation_mask, context);
        if (lexeme.part_of_speech != pos_noun &&
            lexeme.part_of_speech != pos_pronoun &&
            lexeme.part_of_speech != pos_adjective &&
            lexeme.part_of_speech != pos_numeral &&
            lexeme.part_of_speech != pos_adverb &&
            lexeme.part_of_speech != pos_verb &&
            lexeme.part_of_speech != pos_preposition &&
            lexeme.part_of_speech != pos_conjunction &&
            lexeme.part_of_speech != pos_interjection) {
            fail(context + ": part_of_speech is not importable");
        }
        const auto declension =
            static_cast<std::uint8_t>(lexeme.paradigm >> wwdb::pos_width);
        const auto variant = static_cast<std::uint8_t>(
            lexeme.paradigm & wwdb::low_mask<std::uint8_t>(wwdb::pos_width));
        if (declension > maximum_paradigm_component ||
            variant > maximum_paradigm_component) {
            fail(context + ": paradigm component exceeds 0..9");
        }
        const auto age = lexeme.translation & wwdb::age_mask;
        const auto subject =
            (lexeme.translation >> wwdb::subject_shift) & wwdb::subject_mask;
        const auto geography = (lexeme.translation >> wwdb::geography_shift) &
                               wwdb::geography_mask;
        const auto frequency = (lexeme.translation >> wwdb::frequency_shift) &
                               wwdb::frequency_mask;
        const auto source =
            (lexeme.translation >> wwdb::source_shift) & wwdb::source_mask;
        if (age > maximum_lexical_age || subject > maximum_subject_area ||
            geography > maximum_geography ||
            frequency > maximum_lexical_frequency ||
            source != imported_lexeme_source) {
            fail(context +
                 ": translation metadata is outside the import policy");
        }
        if ((lexeme.part_of_speech == pos_noun &&
             ((lexeme.class_payload & wwdb::three_bit_mask) >
                  maximum_noun_gender ||
              (lexeme.class_payload >> wwdb::noun_kind_shift) >
                  std::to_underlying(words::NounKind::place))) ||
            (lexeme.part_of_speech == pos_pronoun &&
             lexeme.class_payload > wwdb::three_bit_mask) ||
            ((lexeme.part_of_speech == pos_adjective ||
              lexeme.part_of_speech == pos_adverb) &&
             lexeme.class_payload > maximum_adjective_degree) ||
            (lexeme.part_of_speech == pos_verb &&
             lexeme.class_payload >
                 std::to_underlying(words::VerbKind::perfect_definite)) ||
            (lexeme.part_of_speech == pos_preposition &&
             lexeme.class_payload > wwdb::three_bit_mask) ||
            (lexeme.part_of_speech == pos_numeral &&
             ((lexeme.class_payload & wwdb::three_bit_mask) >
                  maximum_numeral_type ||
              (lexeme.class_payload >> wwdb::numeral_value_shift) >
                  maximum_imported_numeral_value)) ||
            ((lexeme.part_of_speech == pos_conjunction ||
              lexeme.part_of_speech == pos_interjection) &&
             lexeme.class_payload != 0U)) {
            fail(context + ": class_payload is invalid for part_of_speech");
        }
        if ((lexeme.part_of_speech == pos_numeral &&
             lexeme.numeric_value !=
                 (lexeme.class_payload >> wwdb::numeral_value_shift)) ||
            (lexeme.part_of_speech != pos_numeral &&
             lexeme.numeric_value != 0U)) {
            fail(context + ": numeric_value disagrees with class_payload");
        }
        result.push_back(std::move(lexeme));
    }
    if (!input.eof()) {
        fail("cannot read input: " + path.string());
    }
    return result;
}

void write_file(const std::filesystem::path &path,
                std::span<const std::byte> bytes) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) {
        fail("cannot open output: " + path.string());
    }
    output.write(reinterpret_cast<const char *>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
    if (!output) {
        fail("cannot write output: " + path.string());
    }
}

std::uint8_t byte_at(std::span<const std::byte> bytes, std::size_t offset) {
    if (offset >= bytes.size()) {
        fail("legacy read past end of buffer");
    }
    return std::to_integer<std::uint8_t>(bytes[offset]);
}

std::uint32_t read_u32_le(std::span<const std::byte> bytes,
                          std::size_t offset) {
    if (offset > bytes.size() || bytes.size() - offset < wwdb::u32_size) {
        fail("legacy u32 read past end of buffer");
    }
    return static_cast<std::uint32_t>(byte_at(bytes, offset)) |
           (static_cast<std::uint32_t>(byte_at(bytes, offset + 1))
            << wwdb::bits_per_byte) |
           (static_cast<std::uint32_t>(byte_at(bytes, offset + 2))
            << (2U * wwdb::bits_per_byte)) |
           (static_cast<std::uint32_t>(byte_at(bytes, offset + 3))
            << (3U * wwdb::bits_per_byte));
}

std::uint64_t read_u64_le(std::span<const std::byte> bytes,
                          std::size_t offset) {
    const auto low = read_u32_le(bytes, offset);
    const auto high = read_u32_le(bytes, offset + wwdb::u32_size);
    return static_cast<std::uint64_t>(low) |
           (static_cast<std::uint64_t>(high)
            << (wwdb::u32_size * wwdb::bits_per_byte));
}

void append_u8(Bytes &output, std::uint8_t value) {
    output.push_back(static_cast<std::byte>(value));
}

void append_u16_le(Bytes &output, std::uint16_t value) {
    append_u8(output, static_cast<std::uint8_t>(value));
    append_u8(output, static_cast<std::uint8_t>(value >> wwdb::bits_per_byte));
}

void append_u24_le(Bytes &output, std::uint32_t value) {
    if (value >
        wwdb::low_mask<std::uint32_t>(wwdb::u24_size * wwdb::bits_per_byte)) {
        fail("u24 value is out of range");
    }
    append_u8(output, static_cast<std::uint8_t>(value));
    append_u8(output, static_cast<std::uint8_t>(value >> wwdb::bits_per_byte));
    append_u8(output,
              static_cast<std::uint8_t>(value >> (2U * wwdb::bits_per_byte)));
}

void append_u32_le(Bytes &output, std::uint32_t value) {
    append_u8(output, static_cast<std::uint8_t>(value));
    append_u8(output, static_cast<std::uint8_t>(value >> wwdb::bits_per_byte));
    append_u8(output,
              static_cast<std::uint8_t>(value >> (2U * wwdb::bits_per_byte)));
    append_u8(output,
              static_cast<std::uint8_t>(value >> (3U * wwdb::bits_per_byte)));
}

void append_u48_le(Bytes &output, std::uint64_t value) {
    if (value >
        wwdb::low_mask<std::uint64_t>(wwdb::u48_size * wwdb::bits_per_byte)) {
        fail("u48 value is out of range");
    }
    append_u32_le(output, static_cast<std::uint32_t>(value));
    append_u16_le(output, static_cast<std::uint16_t>(
                              value >> (wwdb::u32_size * wwdb::bits_per_byte)));
}

void append_u64_le(Bytes &output, std::uint64_t value) {
    append_u32_le(output, static_cast<std::uint32_t>(value));
    append_u32_le(output, static_cast<std::uint32_t>(
                              value >> (wwdb::u32_size * wwdb::bits_per_byte)));
}

std::string fixed_string(std::span<const std::byte> bytes, std::size_t offset,
                         std::size_t width) {
    if (offset > bytes.size() || bytes.size() - offset < width) {
        fail("legacy string read past end of buffer");
    }

    std::string result;
    result.resize(width);
    std::memcpy(result.data(), bytes.data() + offset, width);
    while (!result.empty() && result.back() == ' ') {
        result.pop_back();
    }
    return result;
}

std::string_view trim(std::string_view value) {
    while (!value.empty() && value.front() == ' ') {
        value.remove_prefix(1);
    }
    while (!value.empty() && (value.back() == ' ' || value.back() == '\r' ||
                              value.back() == '\t')) {
        value.remove_suffix(1);
    }
    return value;
}

bool quantity_positions_are_vowels(const std::string_view text,
                                   const std::uint32_t known) {
    for (std::size_t index = 0; index < text.size(); ++index) {
        if ((known & (std::uint32_t{1U} << index)) == 0U) {
            continue;
        }
        const auto letter = text[index];
        if (letter != 'a' && letter != 'e' && letter != 'i' && letter != 'o' &&
            letter != 'u' && letter != 'y') {
            return false;
        }
    }
    return true;
}

std::vector<std::string_view> split_words(std::string_view value) {
    std::vector<std::string_view> result;
    while (true) {
        value = trim(value);
        if (value.empty()) {
            return result;
        }
        const auto end = value.find_first_of(" \t");
        result.push_back(value.substr(0, end));
        if (end == std::string_view::npos) {
            return result;
        }
        value.remove_prefix(end);
    }
}

std::uint8_t parse_u8(std::string_view value, std::string_view field,
                      std::string_view source = "ADDONS.LAT") {
    unsigned parsed = 0;
    const auto [end, error] =
        std::from_chars(value.data(), value.data() + value.size(), parsed);
    if (error != std::errc{} || end != value.data() + value.size() ||
        parsed > std::numeric_limits<std::uint8_t>::max()) {
        fail("invalid " + std::string(field) + " in " + std::string(source) +
             ": " + std::string(value));
    }
    return static_cast<std::uint8_t>(parsed);
}

std::uint32_t parse_u32(std::string_view value, std::string_view field,
                        std::string_view source) {
    std::uint32_t parsed{};
    const auto [end, error] =
        std::from_chars(value.data(), value.data() + value.size(), parsed);
    if (error != std::errc{} || end != value.data() + value.size()) {
        fail("invalid " + std::string(field) + " in " + std::string(source) +
             ": " + std::string(value));
    }
    return parsed;
}

std::uint8_t enum_value(std::string_view value,
                        std::initializer_list<std::string_view> names,
                        std::string_view field,
                        std::string_view source = "ADDONS.LAT") {
    const auto ascii_equal = [](const std::string_view left,
                                const std::string_view right) {
        return std::ranges::equal(left, right, [](char first, char second) {
            const auto lower = [](const char character) {
                return character >= 'A' && character <= 'Z'
                           ? static_cast<char>(character - 'A' + 'a')
                           : character;
            };
            return lower(first) == lower(second);
        });
    };
    const auto found =
        std::ranges::find_if(names, [&](const std::string_view name) {
            return ascii_equal(name, value);
        });
    if (found == names.end()) {
        fail("invalid " + std::string(field) + " in " + std::string(source) +
             ": " + std::string(value));
    }
    return static_cast<std::uint8_t>(std::distance(names.begin(), found));
}

template <std::size_t Size>
std::uint8_t enum_value(std::string_view value,
                        const std::array<std::string_view, Size> &names,
                        std::string_view field, std::string_view source) {
    const auto found = std::ranges::find(names, value);
    if (found == names.end()) {
        fail("invalid " + std::string(field) + " in " + std::string(source) +
             ": " + std::string(value));
    }
    return static_cast<std::uint8_t>(std::distance(names.begin(), found));
}

std::uint8_t part_of_speech(std::string_view value,
                            std::string_view source = "ADDONS.LAT") {
    constexpr std::array<std::string_view, 8> names{
        "X", "N", "PRON", "PACK", "ADJ", "NUM", "ADV", "V"};
    const auto found = std::ranges::find(names, value);
    if (found == names.end()) {
        fail("invalid part of speech in " + std::string(source) + ": " +
             std::string(value));
    }
    return static_cast<std::uint8_t>(std::distance(names.begin(), found));
}

SuffixSource parse_suffix(const std::uint16_t addon_id,
                          const std::vector<std::string_view> &header,
                          const std::vector<std::string_view> &target,
                          std::string meaning) {
    if (header.size() < 2 || header.size() > 3 ||
        target.size() < suffix_fields::minimum_target_count) {
        fail("invalid SUFFIX record shape in ADDONS.LAT");
    }

    SuffixSource result;
    result.addon_id = addon_id;
    result.fix = header[1];
    result.meaning = std::move(meaning);
    if (header.size() == 3) {
        if (header[2].size() != 1) {
            fail("suffix connector must be one ASCII character");
        }
        result.connect = static_cast<std::uint8_t>(header[2].front());
    }
    result.root = part_of_speech(target[0]);
    result.root_key = parse_u8(target[1], "suffix root key");
    result.target = part_of_speech(target[2]);

    const auto paradigm = [&](const std::size_t which_index,
                              const std::size_t variant_index) {
        return pack_paradigm(parse_u8(target.at(which_index), "paradigm"),
                             parse_u8(target.at(variant_index), "variant"));
    };
    switch (result.target) {
    case pos_noun: // noun
        if (target.size() != suffix_fields::noun_or_numeral_count) {
            fail("invalid noun suffix target in ADDONS.LAT");
        }
        result.paradigm = paradigm(3, 4);
        result.attribute_0 = enum_value(target[suffix_fields::attribute],
                                        {"X", "M", "F", "N", "C"}, "gender");
        result.attribute_1 = enum_value(
            target[suffix_fields::second_attribute],
            {"x", "s", "m", "a", "g", "n", "p", "t", "l", "w"}, "noun kind");
        result.target_key =
            parse_u8(target[suffix_fields::key_after_two_attributes],
                     "suffix target key");
        break;
    case pos_adjective: // adjective
        if (target.size() != suffix_fields::adjective_or_verb_count) {
            fail("invalid adjective suffix target in ADDONS.LAT");
        }
        result.paradigm = paradigm(3, 4);
        result.attribute_0 =
            enum_value(target[suffix_fields::attribute],
                       {"X", "POS", "COMP", "SUPER"}, "degree");
        result.target_key =
            parse_u8(target[suffix_fields::key_after_one_attribute],
                     "suffix target key");
        break;
    case pos_numeral: // numeral
        if (target.size() != suffix_fields::noun_or_numeral_count) {
            fail("invalid numeral suffix target in ADDONS.LAT");
        }
        result.paradigm = paradigm(3, 4);
        result.attribute_0 =
            enum_value(target[suffix_fields::attribute],
                       {"X", "CARD", "ORD", "DIST", "ADVERB"}, "numeral sort");
        result.numeric_value =
            parse_u8(target[suffix_fields::second_attribute], "numeral value");
        result.target_key =
            parse_u8(target[suffix_fields::key_after_two_attributes],
                     "suffix target key");
        break;
    case pos_adverb: // adverb
        if (target.size() != suffix_fields::minimum_target_count) {
            fail("invalid adverb suffix target in ADDONS.LAT");
        }
        result.attribute_0 =
            enum_value(target[3], {"X", "POS", "COMP", "SUPER"}, "degree");
        result.target_key = parse_u8(target[4], "suffix target key");
        break;
    case pos_verb: // verb
        if (target.size() != suffix_fields::adjective_or_verb_count) {
            fail("invalid verb suffix target in ADDONS.LAT");
        }
        result.paradigm = paradigm(3, 4);
        result.attribute_0 =
            enum_value(target[suffix_fields::attribute], {"X"}, "verb kind");
        result.target_key =
            parse_u8(target[suffix_fields::key_after_one_attribute],
                     "suffix target key");
        break;
    default:
        fail("unsupported suffix target in ADDONS.LAT");
    }
    if (result.root_key > 4 || result.target_key > 4) {
        fail("suffix stem key is outside the observed 0..4 range");
    }
    return result;
}

PrefixSource parse_prefix(const std::uint16_t addon_id,
                          const std::vector<std::string_view> &header,
                          const std::vector<std::string_view> &target,
                          std::string meaning) {
    if (header.size() < 2 || header.size() > 3 || target.size() != 2) {
        fail("invalid PREFIX record shape in ADDONS.LAT");
    }
    PrefixSource result;
    result.addon_id = addon_id;
    result.fix = header[1];
    result.meaning = std::move(meaning);
    if (header.size() == 3) {
        if (header[2].size() != 1) {
            fail("prefix connector must be one ASCII character");
        }
        result.connect = static_cast<std::uint8_t>(header[2].front());
    }
    result.root = part_of_speech(target[0]);
    result.target = part_of_speech(target[1]);
    if (result.root != result.target) {
        fail("legacy PREFIX root and target differ in ADDONS.LAT");
    }
    return result;
}

TackonSource parse_tackon(const std::uint16_t addon_id,
                          const std::vector<std::string_view> &header,
                          const std::vector<std::string_view> &target,
                          std::string meaning,
                          const std::size_t tackon_ordinal) {
    if (header.size() != 2 || target.empty()) {
        fail("invalid TACKON record shape in ADDONS.LAT");
    }
    TackonSource result;
    result.addon_id = addon_id;
    result.fix = header[1];
    result.meaning = std::move(meaning);
    result.base = part_of_speech(target[0]);
    result.enclitic = tackon_ordinal < 4U;

    const auto paradigm = [&]() {
        if (target.size() < 3U) {
            fail("missing tackon paradigm in ADDONS.LAT");
        }
        return pack_paradigm(parse_u8(target[1], "tackon declension"),
                             parse_u8(target[2], "tackon variant"));
    };
    switch (result.base) {
    case 0: // X
        if (target.size() != 1U) {
            fail("invalid generic tackon target in ADDONS.LAT");
        }
        break;
    case pos_noun: // noun
        if (target.size() != tackon_noun_target_count) {
            fail("invalid noun tackon target in ADDONS.LAT");
        }
        result.paradigm = paradigm();
        result.attribute_0 =
            enum_value(target[3], {"X", "M", "F", "N", "C"}, "gender");
        result.attribute_1 = enum_value(
            target[4], {"x", "s", "m", "a", "g", "n", "p", "t", "l", "w"},
            "noun kind");
        break;
    case pos_pronoun: // pronoun
    case pos_pack:    // pack
        if (target.size() != 4U) {
            fail("invalid pronoun tackon target in ADDONS.LAT");
        }
        result.paradigm = paradigm();
        result.attribute_0 = enum_value(target[3],
                                        {"X", "PERS", "REL", "REFLEX", "DEMONS",
                                         "INTERR", "INDEF", "ADJECT"},
                                        "pronoun kind");
        break;
    case pos_adjective: // adjective
        if (target.size() != 4U) {
            fail("invalid adjective tackon target in ADDONS.LAT");
        }
        result.paradigm = paradigm();
        result.attribute_0 =
            enum_value(target[3], {"X", "POS", "COMP", "SUPER"}, "degree");
        break;
    default:
        fail("unsupported tackon target in ADDONS.LAT");
    }
    const auto which = static_cast<std::uint8_t>(result.paradigm >> 4U);
    // PACK is a distinct Target_Entry variant in the Ada source.  Do not use
    // the English gloss as a classifier: the base class and its observed
    // paradigm are the structural legacy predicate.
    result.packon = result.base == 3U && (which == 1U || which == 2U);
    return result;
}

std::unordered_map<std::uint32_t, std::string>
read_packon_requirements(const std::filesystem::path &path) {
    std::ifstream input(path);
    if (!input) {
        fail("cannot open input: " + path.string());
    }

    std::unordered_map<std::uint32_t, std::string> result;
    std::string line;
    std::size_t line_number = 0U;
    while (std::getline(input, line)) {
        ++line_number;
        const auto clean = trim(line);
        if (clean.empty() || clean.starts_with("--")) {
            continue;
        }
        const auto separator = clean.find_first_of(" \t");
        if (separator == std::string_view::npos) {
            fail("invalid PACKON_REQUIREMENTS.LAT line " +
                 std::to_string(line_number));
        }
        std::uint32_t entry{};
        const auto number = clean.substr(0U, separator);
        const auto [end, error] = std::from_chars(
            number.data(), number.data() + number.size(), entry);
        const auto fix = trim(clean.substr(separator));
        if (error != std::errc{} || end != number.data() + number.size() ||
            entry == 0U || fix.empty() ||
            !std::ranges::all_of(fix,
                                 [](const char character) {
                                     return character >= 'a' &&
                                            character <= 'z';
                                 }) ||
            !result.emplace(entry, fix).second) {
            fail("invalid PACKON_REQUIREMENTS.LAT entry at line " +
                 std::to_string(line_number));
        }
    }
    if (!input.eof()) {
        fail("cannot read input: " + path.string());
    }
    return result;
}

AddonSources read_addons(const std::filesystem::path &path) {
    std::ifstream input(path);
    if (!input) {
        fail("cannot open input: " + path.string());
    }

    const auto next_data_line = [&]() -> std::string {
        std::string line;
        while (std::getline(input, line)) {
            const auto clean = trim(line);
            if (!clean.empty() && !clean.starts_with("--")) {
                return std::string(clean);
            }
        }
        return {};
    };

    AddonSources result;
    std::uint32_t addon_ordinal = 0;
    while (true) {
        auto header_line = next_data_line();
        if (header_line.empty()) {
            break;
        }
        auto target_line = next_data_line();
        auto meaning_line = next_data_line();
        if (target_line.empty() || meaning_line.empty()) {
            fail("incomplete ADDONS.LAT record");
        }
        // Ada's Meaning_Type is fixed at 80 bytes. Preserve its observable
        // truncation rather than leaking longer source lines into JSON.
        if (meaning_line.size() > legacy_meaning_width) {
            meaning_line.resize(legacy_meaning_width);
        }
        if (const auto comment = header_line.find("--");
            comment != std::string::npos) {
            header_line.erase(comment);
        }
        const auto header = split_words(header_line);
        if (header.empty()) {
            fail("empty ADDONS.LAT header");
        }
        if (addon_ordinal > std::numeric_limits<std::uint16_t>::max()) {
            fail("PoC u16 addon ID capacity exceeded");
        }
        const auto addon_id = static_cast<std::uint16_t>(addon_ordinal);
        ++addon_ordinal;
        if (header.front() == "PREFIX") {
            result.prefixes.push_back(parse_prefix(addon_id, header,
                                                   split_words(target_line),
                                                   std::move(meaning_line)));
        } else if (header.front() == "SUFFIX") {
            result.suffixes.push_back(parse_suffix(addon_id, header,
                                                   split_words(target_line),
                                                   std::move(meaning_line)));
        } else if (header.front() == "TACKON") {
            result.tackons.push_back(
                parse_tackon(addon_id, header, split_words(target_line),
                             std::move(meaning_line), result.tackons.size()));
        } else {
            fail("unknown ADDONS.LAT record kind: " +
                 std::string(header.front()));
        }
    }
    return result;
}

UniqueSource parse_unique(const std::string_view surface,
                          const std::vector<std::string_view> &fields,
                          std::string meaning) {
    constexpr std::string_view source{"UNIQUES.LAT"};
    if (surface.empty() || surface.size() > dictionary_stem_width) {
        fail("unique surface is empty or exceeds the legacy stem width");
    }
    if (meaning.size() > legacy_meaning_width) {
        // Meaning_Type is fixed-width in Ada, so retaining the truncation is
        // required for byte-for-byte canonical JSON compatibility.
        meaning.resize(legacy_meaning_width);
    }

    UniqueSource result;
    result.surface = std::string{surface};
    result.meaning = std::move(meaning);
    if (fields.empty()) {
        fail("empty UNIQUES.LAT quality line");
    }
    result.part_of_speech = part_of_speech(fields.front(), source);

    const auto paradigm = [&](const std::size_t which_index,
                              const std::size_t variant_index) {
        return pack_paradigm(
            parse_u8(fields.at(which_index), "unique paradigm", source),
            parse_u8(fields.at(variant_index), "unique variant", source));
    };
    const auto nominal_morphology = [&]() {
        const auto grammatical_case =
            enum_value(fields.at(3),
                       {"X", "NOM", "VOC", "GEN", "LOC", "DAT", "ABL", "ACC"},
                       "unique case", source);
        const auto number =
            enum_value(fields.at(4), {"X", "S", "P"}, "unique number", source);
        const auto gender =
            enum_value(fields.at(unique_fields::gender),
                       {"X", "M", "F", "N", "C"}, "unique gender", source);
        return static_cast<std::uint16_t>(
            static_cast<std::uint16_t>(grammatical_case) |
            (static_cast<std::uint16_t>(number) << 3U) |
            (static_cast<std::uint16_t>(gender) << wwdb::nominal_gender_shift));
    };

    std::size_t translation_offset = 0;
    switch (result.part_of_speech) {
    case pos_noun: // noun
        if (fields.size() != unique_fields::nominal_count) {
            fail("invalid noun record shape in UNIQUES.LAT");
        }
        result.paradigm = paradigm(1, 2);
        result.morphology = nominal_morphology();
        static_cast<void>(
            enum_value(fields[unique_fields::nominal_attribute],
                       {"X", "S", "M", "A", "G", "N", "P", "T", "L", "W"},
                       "unique noun kind", source));
        translation_offset = unique_fields::nominal_translation;
        break;
    case pos_pronoun: // pronoun
        if (fields.size() != unique_fields::nominal_count) {
            fail("invalid pronoun record shape in UNIQUES.LAT");
        }
        result.paradigm = paradigm(1, 2);
        result.morphology = nominal_morphology();
        static_cast<void>(enum_value(fields[unique_fields::nominal_attribute],
                                     {"X", "PERS", "REL", "REFLEX", "DEMONS",
                                      "INTERR", "INDEF", "ADJECT"},
                                     "unique pronoun kind", source));
        translation_offset = unique_fields::nominal_translation;
        break;
    case pos_adjective: { // adjective
        if (fields.size() != unique_fields::nominal_count) {
            fail("invalid adjective record shape in UNIQUES.LAT");
        }
        result.paradigm = paradigm(1, 2);
        const auto degree =
            enum_value(fields[unique_fields::nominal_attribute],
                       {"X", "POS", "COMP", "SUPER"}, "unique degree", source);
        result.morphology = static_cast<std::uint16_t>(
            nominal_morphology() | (static_cast<std::uint16_t>(degree)
                                    << wwdb::morphology_byte_shift));
        translation_offset = unique_fields::nominal_translation;
        break;
    }
    case pos_verb: { // verb
        if (fields.size() != unique_fields::verb_count) {
            fail("invalid verb record shape in UNIQUES.LAT");
        }
        result.paradigm = paradigm(1, 2);
        const auto tense = enum_value(
            fields[3], {"X", "PRES", "IMPF", "FUT", "PERF", "PLUP", "FUTP"},
            "unique tense", source);
        const auto voice = enum_value(fields[4], {"X", "ACTIVE", "PASSIVE"},
                                      "unique voice", source);
        const auto mood =
            enum_value(fields[5], {"X", "IND", "SUB", "IMP", "INF", "PPL"},
                       "unique mood", source);
        const auto person = parse_u8(fields[unique_fields::verb_person],
                                     "unique person", source);
        const auto number =
            enum_value(fields[unique_fields::verb_number], {"X", "S", "P"},
                       "unique number", source);
        if (person > 3U) {
            fail("unique verb person is outside 0..3");
        }
        static_cast<void>(
            enum_value(fields[unique_fields::verb_kind],
                       {"X", "TO_BE", "TO_BEING", "GEN", "DAT", "ABL", "TRANS",
                        "INTRANS", "IMPERS", "DEP", "SEMIDEP", "PERFDEF"},
                       "unique verb kind", source));
        result.morphology = static_cast<std::uint16_t>(
            static_cast<std::uint16_t>(tense) |
            (static_cast<std::uint16_t>(voice) << 3U) |
            (static_cast<std::uint16_t>(mood) << wwdb::nominal_gender_shift) |
            (static_cast<std::uint16_t>(person)
             << wwdb::morphology_byte_shift) |
            (static_cast<std::uint16_t>(number) << wwdb::verb_number_shift));
        translation_offset = unique_fields::verb_translation;
        break;
    }
    default:
        fail("unsupported part of speech in UNIQUES.LAT");
    }

    const auto age = enum_value(fields.at(translation_offset),
                                {"X", "A", "B", "C", "D", "E", "F", "G", "H"},
                                "unique age", source);
    const auto area =
        enum_value(fields.at(translation_offset + 1U),
                   {"X", "A", "B", "D", "E", "G", "L", "P", "S", "T", "W", "Y"},
                   "unique area", source);
    const auto geography =
        enum_value(fields.at(translation_offset + 2U),
                   {"X", "A", "B", "C", "D", "E", "F", "G", "H", "I", "J", "K",
                    "N", "P", "Q", "R", "S", "U"},
                   "unique geography", source);
    const auto frequency =
        enum_value(fields.at(translation_offset + 3U),
                   {"X", "A", "B", "C", "D", "E", "F", "I", "M", "N"},
                   "unique frequency", source);
    const auto dictionary_source = enum_value(
        fields.at(translation_offset + 4U),
        {"X", "A", "B", "C", "D", "E", "F", "G", "H", "I", "J", "K", "L",
         "M", "N", "O", "P", "Q", "R", "S", "T", "U", "V", "W", "Y", "Z"},
        "unique source", source);
    result.translation =
        static_cast<std::uint32_t>(age) |
        (static_cast<std::uint32_t>(area) << 4U) |
        (static_cast<std::uint32_t>(geography) << wwdb::morphology_byte_shift) |
        (static_cast<std::uint32_t>(frequency) << wwdb::frequency_shift) |
        (static_cast<std::uint32_t>(dictionary_source) << wwdb::source_shift);
    return result;
}

std::vector<UniqueSource> read_uniques(const std::filesystem::path &path) {
    std::ifstream input(path);
    if (!input) {
        fail("cannot open input: " + path.string());
    }

    std::vector<UniqueSource> result;
    std::string surface;
    while (std::getline(input, surface)) {
        surface = std::string{trim(surface)};
        if (surface.empty() || surface.starts_with("--")) {
            continue;
        }
        std::string quality;
        std::string meaning;
        if (!std::getline(input, quality) || !std::getline(input, meaning)) {
            fail("incomplete UNIQUES.LAT record");
        }
        result.push_back(parse_unique(surface, split_words(quality),
                                      std::string{trim(meaning)}));
    }
    return result;
}

std::vector<RewriteSource> read_rewrites(const std::filesystem::path &path) {
    constexpr std::string_view source{"REWRITES.LAT"};
    std::ifstream input(path);
    if (!input) {
        fail("cannot open input: " + path.string());
    }

    const auto next_data_line = [&]() -> std::string {
        std::string line;
        while (std::getline(input, line)) {
            const auto clean = trim(line);
            if (!clean.empty() && !clean.starts_with("--")) {
                return std::string{clean};
            }
        }
        return {};
    };

    std::vector<RewriteSource> result;
    while (true) {
        const auto header_line = next_data_line();
        if (header_line.empty()) {
            break;
        }
        auto meaning = next_data_line();
        if (meaning.empty()) {
            fail("incomplete REWRITES.LAT record");
        }
        const auto fields = split_words(header_line);
        if (fields.size() != rewrite_fields::count) {
            fail("invalid REWRITES.LAT record shape");
        }
        if (meaning.size() > legacy_meaning_width) {
            meaning.resize(legacy_meaning_width);
        }

        RewriteSource rewrite;
        rewrite.kind = enum_value(fields[0], {"X", "SYNCOPE", "ORTHOGRAPHIC"},
                                  "rewrite kind", source);
        rewrite.priority = parse_u8(fields[1], "rewrite priority", source);
        rewrite.stage =
            enum_value(fields[2], {"X", "MAIN", "EARLY", "FALLBACK"},
                       "rewrite stage", source);
        rewrite.operation =
            enum_value(fields[3], {"X", "LITERAL", "SLUR", "DOUBLE_CONSONANT"},
                       "rewrite operation", source);
        rewrite.scope =
            enum_value(fields[4], {"X", "INITIAL", "INTERNAL", "FINAL"},
                       "rewrite scope", source);
        rewrite.scan_reverse = enum_value(fields[rewrite_fields::direction],
                                          {"FORWARD", "REVERSE"},
                                          "rewrite direction", source) == 1U;
        rewrite.before = fields[rewrite_fields::before] == "-"
                             ? ""
                             : std::string{fields[rewrite_fields::before]};
        rewrite.after = fields[rewrite_fields::after] == "-"
                            ? ""
                            : std::string{fields[rewrite_fields::after]};
        rewrite.required_part =
            part_of_speech(fields[rewrite_fields::required_part], source);
        rewrite.required_stem_key =
            parse_u8(fields[rewrite_fields::required_stem_key],
                     "required stem key", source);
        rewrite.minimum_before =
            parse_u8(fields[rewrite_fields::minimum_before],
                     "minimum prefix size", source);
        rewrite.minimum_after = parse_u8(fields[rewrite_fields::minimum_after],
                                         "minimum suffix size", source);
        rewrite.constraint = enum_value(fields[rewrite_fields::constraint],
                                        {"ANY", "EO_VERB", "ADJECTIVE_IIS"},
                                        "rewrite constraint", source);
        rewrite.medieval =
            enum_value(fields[rewrite_fields::era], {"CLASSICAL", "MEDIEVAL"},
                       "rewrite era", source) == 1U;
        rewrite.name = fields[rewrite_fields::name];
        rewrite.meaning = std::move(meaning);
        if (rewrite.kind == 0U || rewrite.scope == 0U ||
            rewrite.operation == 0U || rewrite.stage == 0U ||
            rewrite.required_stem_key > 4U || rewrite.name.empty() ||
            (rewrite.before.empty() && rewrite.operation != 3U) ||
            rewrite.minimum_before > maximum_rewrite_context_size ||
            rewrite.minimum_after > maximum_rewrite_context_size) {
            fail("invalid REWRITES.LAT rewrite constraints");
        }
        result.push_back(std::move(rewrite));
    }
    return result;
}

QuantitySources read_quantities(const std::filesystem::path &path) {
    constexpr std::string_view source{"QUANTITIES.LAT"};
    std::ifstream input(path);
    if (!input) {
        fail("cannot open input: " + path.string());
    }

    QuantitySources result;
    std::string line;
    while (std::getline(input, line)) {
        if (const auto comment = line.find(ada_comment_marker);
            comment != std::string::npos) {
            line.erase(comment);
        }
        const auto fields = split_words(line);
        if (fields.empty()) {
            continue;
        }
        if (fields.front() == "INFLECTION" && fields.size() == 4U) {
            const auto rule_id = parse_u32(fields[1], "rule ID", source);
            if (rule_id > std::numeric_limits<std::uint16_t>::max()) {
                fail("inflection rule ID exceeds u16 in QUANTITIES.LAT");
            }
            result.inflections.push_back({
                static_cast<std::uint16_t>(rule_id),
                parse_u32(fields[2], "known mask", source),
                parse_u32(fields[3], "long mask", source),
            });
            continue;
        }
        if (fields.front() == "STEM" &&
            fields.size() == quantity_fields::stem_count) {
            const auto entry = parse_u32(fields[1], "dictionary entry", source);
            if (entry == 0U ||
                entry > std::numeric_limits<std::uint16_t>::max()) {
                fail(
                    "dictionary entry exceeds one-based u16 in QUANTITIES.LAT");
            }
            const auto slot = parse_u8(fields[2], "lexical slot", source);
            if (slot < 1U || slot > 4U) {
                fail("lexical slot is outside 1..4 in QUANTITIES.LAT");
            }
            result.stems.push_back({
                static_cast<std::uint16_t>(entry),
                slot,
                parse_u32(fields[3], "known mask", source),
                parse_u32(fields[4], "long mask", source),
            });
            continue;
        }
        if (fields.front() == "SUFFIX" &&
            fields.size() == quantity_fields::suffix_count) {
            const auto addon_id = parse_u32(fields[1], "addon ID", source);
            if (addon_id > std::numeric_limits<std::uint16_t>::max()) {
                fail("suffix addon ID exceeds u16 in QUANTITIES.LAT");
            }
            result.suffixes.push_back({
                static_cast<std::uint16_t>(addon_id),
                std::string{fields[2]},
                part_of_speech(fields[3], source),
                parse_u8(fields[4], "suffix root key", source),
                part_of_speech(fields[quantity_fields::suffix_target_part],
                               source),
                parse_u8(fields[quantity_fields::suffix_target_key],
                         "suffix target key", source),
                parse_u8(fields[quantity_fields::suffix_root_declension],
                         "suffix root declension", source),
                parse_u8(fields[quantity_fields::suffix_root_variant],
                         "suffix root variant", source),
                parse_u32(fields[quantity_fields::suffix_known], "known mask",
                          source),
                parse_u32(fields[quantity_fields::suffix_long], "long mask",
                          source),
            });
            continue;
        }
        fail("invalid QUANTITIES.LAT record shape: " + line);
    }
    return result;
}

std::vector<SuffixPolicySource>
read_suffix_policies(const std::filesystem::path &path) {
    constexpr std::string_view source{"ADDON_POLICIES.LAT"};
    std::ifstream input(path);
    if (!input) {
        fail("cannot open input: " + path.string());
    }
    std::vector<SuffixPolicySource> result;
    std::string line;
    while (std::getline(input, line)) {
        if (const auto comment = line.find(ada_comment_marker);
            comment != std::string::npos) {
            line.erase(comment);
        }
        const auto fields = split_words(line);
        if (fields.empty()) {
            continue;
        }
        if (fields.size() != policy_fields::count || fields[0] != "SUFFIX" ||
            fields[policy_fields::terminator] != "COEXIST_REGULAR") {
            fail("invalid ADDON_POLICIES.LAT record shape: " + line);
        }
        const auto id = parse_u32(fields[1], "addon ID", source);
        if (id > std::numeric_limits<std::uint16_t>::max() ||
            (fields[policy_fields::connector] != "-" &&
             fields[policy_fields::connector].size() != 1U)) {
            fail("invalid suffix policy ID or connector");
        }
        const auto root_declension =
            parse_u8(fields[policy_fields::root_declension],
                     "suffix root declension", source);
        const auto root_variant = parse_u8(fields[policy_fields::root_variant],
                                           "suffix root variant", source);
        if (root_declension > maximum_paradigm_component ||
            root_variant > maximum_paradigm_component) {
            fail("suffix source paradigm exceeds 0..9 in ADDON_POLICIES.LAT");
        }
        result.push_back({
            static_cast<std::uint16_t>(id),
            std::string{fields[2]},
            part_of_speech(fields[3], source),
            parse_u8(fields[4], "suffix root key", source),
            part_of_speech(fields[policy_fields::target_part], source),
            parse_u8(fields[policy_fields::target_key], "suffix target key",
                     source),
            enum_value(fields[policy_fields::target_degree],
                       {"X", "POS", "COMP", "SUPER"}, "suffix target degree",
                       source),
            fields[policy_fields::connector] == "-"
                ? '\0'
                : fields[policy_fields::connector].front(),
            root_declension,
            root_variant,
        });
    }
    return result;
}

std::vector<MorphologicalNoticeSource>
read_morphological_notices(const std::filesystem::path &path) {
    constexpr std::string_view entry_field{"dictionary entry"};
    constexpr std::string_view trigger_field{"morphological notice trigger"};
    constexpr std::string_view notice_field{"morphological notice value"};
    std::ifstream input(path);
    if (!input) {
        fail("cannot open input: " + path.string());
    }

    std::vector<MorphologicalNoticeSource> result;
    std::string line;
    while (std::getline(input, line)) {
        if (const auto comment = line.find(ada_comment_marker);
            comment != std::string::npos) {
            line.erase(comment);
        }
        const auto fields = split_words(line);
        if (fields.empty()) {
            continue;
        }
        if (fields.size() != 3U) {
            fail("invalid " + std::string(morphological_notices_file) +
                 " record shape: " + line);
        }
        const auto entry =
            parse_u32(fields[0], entry_field, morphological_notices_file);
        if (entry == 0U || entry > std::numeric_limits<std::uint16_t>::max()) {
            fail("dictionary entry exceeds one-based u16 in " +
                 std::string(morphological_notices_file));
        }
        result.push_back({
            static_cast<std::uint16_t>(entry),
            enum_value(fields[1], whitaker_trim_reason_names, trigger_field,
                       morphological_notices_file),
            enum_value(fields[2], morphological_notice_names, notice_field,
                       morphological_notices_file),
        });
    }
    return result;
}

class StringPool {
  public:
    std::uint16_t intern(const std::string &value) {
        if (const auto found = ids_.find(value); found != ids_.end()) {
            return found->second;
        }
        if (values_.size() >=
            std::numeric_limits<std::uint16_t>::max() + 1ULL) {
            fail("PoC u16 string ID capacity exceeded");
        }
        if (value.size() > std::numeric_limits<std::uint8_t>::max()) {
            fail("PoC u8 string length capacity exceeded");
        }

        const auto id = static_cast<std::uint16_t>(values_.size());
        values_.push_back(value);
        ids_.emplace(values_.back(), id);
        return id;
    }

    std::uint16_t id_of(const std::string &value) const {
        const auto found = ids_.find(value);
        if (found == ids_.end()) {
            fail("string referenced before interning: " + value);
        }
        return found->second;
    }

    Bytes encode() const {
        Bytes output;
        for (const auto &value : values_) {
            append_u8(output, static_cast<std::uint8_t>(value.size()));
            for (const char ch : value) {
                append_u8(output, static_cast<std::uint8_t>(ch));
            }
        }
        return output;
    }

    std::uint32_t size() const {
        return static_cast<std::uint32_t>(values_.size());
    }

  private:
    std::vector<std::string> values_;
    std::unordered_map<std::string, std::uint16_t> ids_;
};

bool has_paradigm(std::uint8_t pofs) {
    return pofs == pos_noun || pofs == pos_pronoun || pofs == pos_pack ||
           pofs == pos_adjective || pofs == pos_numeral || pofs == pos_verb ||
           pofs == pos_participle || pofs == pos_supine;
}

std::uint8_t pack_paradigm(std::uint32_t which, std::uint32_t variant) {
    if (which > maximum_paradigm_component ||
        variant > maximum_paradigm_component) {
        fail("paradigm component outside 0..9");
    }
    return static_cast<std::uint8_t>((which << wwdb::pos_width) | variant);
}

std::uint32_t pack_translation(std::span<const std::byte> record) {
    const auto age = byte_at(record, dictionary_age_offset);
    const auto area = byte_at(record, dictionary_area_offset);
    const auto geo = byte_at(record, dictionary_geography_offset);
    const auto frequency = byte_at(record, dictionary_frequency_offset);
    const auto source = byte_at(record, dictionary_source_offset);

    if (age > maximum_lexical_age || area > maximum_subject_area ||
        geo > maximum_geography || frequency > maximum_lexical_frequency ||
        source > maximum_legacy_source) {
        fail("dictionary metadata enum outside legacy range");
    }

    return static_cast<std::uint32_t>(age) |
           (static_cast<std::uint32_t>(area) << wwdb::subject_shift) |
           (static_cast<std::uint32_t>(geo) << wwdb::geography_shift) |
           (static_cast<std::uint32_t>(frequency) << wwdb::frequency_shift) |
           (static_cast<std::uint32_t>(source) << wwdb::source_shift);
}

std::uint16_t pack_inflection_morphology(std::span<const std::byte> record,
                                         std::uint8_t pofs) {
    auto at = [&](std::size_t offset) { return byte_at(record, offset); };

    switch (pofs) {
    case pos_noun:    // noun
    case pos_pronoun: // pronoun
    case pos_pack:    // pack
    case pos_supine:  // supine
        return static_cast<std::uint16_t>(at(inflection_morphology_offset) |
                                          (at(inflection_morphology_offset + 1U)
                                           << wwdb::nominal_number_shift) |
                                          (at(inflection_morphology_offset + 2U)
                                           << wwdb::nominal_gender_shift));
    case pos_adjective: // adjective
    case pos_numeral:   // numeral
        return static_cast<std::uint16_t>(at(inflection_morphology_offset) |
                                          (at(inflection_morphology_offset + 1U)
                                           << wwdb::nominal_number_shift) |
                                          (at(inflection_morphology_offset + 2U)
                                           << wwdb::nominal_gender_shift) |
                                          (at(inflection_morphology_offset + 3U)
                                           << wwdb::morphology_byte_shift));
    case pos_adverb:      // adverb
    case pos_preposition: // preposition
        return at(inflection_adverb_case_offset);
    case pos_verb: // finite verb
        return static_cast<std::uint16_t>(
            at(inflection_morphology_offset) |
            (at(inflection_morphology_offset + 1U)
             << wwdb::nominal_number_shift) |
            (at(inflection_morphology_offset + 2U)
             << wwdb::nominal_gender_shift) |
            (at(inflection_morphology_offset + 3U)
             << wwdb::morphology_byte_shift) |
            (at(inflection_morphology_offset + 4U) << wwdb::verb_number_shift));
    case pos_participle: // participle
        return static_cast<std::uint16_t>(at(inflection_morphology_offset) |
                                          (at(inflection_morphology_offset + 1U)
                                           << wwdb::nominal_number_shift) |
                                          (at(inflection_morphology_offset + 2U)
                                           << wwdb::nominal_gender_shift) |
                                          (at(inflection_morphology_offset + 3U)
                                           << wwdb::morphology_byte_shift) |
                                          (at(inflection_morphology_offset + 4U)
                                           << wwdb::participle_voice_shift) |
                                          (at(inflection_participle_mood_offset)
                                           << wwdb::participle_mood_shift));
    default:
        return 0;
    }
}

char canonical_stem_character(char value) {
    if (value >= 'A' && value <= 'Z') {
        value = static_cast<char>(value - 'A' + 'a');
    }
    // The legacy stem index folds the Latin orthographic pairs i/j and u/v.
    if (value == 'j') {
        return 'i';
    }
    if (value == 'v') {
        return 'u';
    }
    return value;
}

std::size_t stem_bucket(std::string_view stem) {
    if (stem.empty()) {
        return 0;
    }
    const auto first_character = canonical_stem_character(stem.front());
    if (first_character < 'a' || first_character > 'z') {
        fail("stem outside lowercase a-z index: " + std::string(stem));
    }

    const auto first = static_cast<std::size_t>(first_character - 'a');
    const auto base = 1 + (first * 27);
    if (stem.size() == 1) {
        return base;
    }
    const auto second_character = canonical_stem_character(stem[1]);
    if (second_character < 'a' || second_character > 'z') {
        fail("second stem character outside lowercase a-z index: " +
             std::string(stem));
    }
    return base + 1 + static_cast<std::size_t>(second_character - 'a');
}

std::strong_ordering canonical_stem_compare(const std::string_view left,
                                            const std::string_view right) {
    const auto common = std::min(left.size(), right.size());
    for (std::size_t index = 0; index < common; ++index) {
        const auto left_value = canonical_stem_character(left[index]);
        const auto right_value = canonical_stem_character(right[index]);
        if (left_value != right_value) {
            return left_value <=> right_value;
        }
    }
    return left.size() <=> right.size();
}

std::uint32_t crc32(std::span<const std::byte> bytes,
                    std::uint32_t crc = wwdb::crc32_initial_value) {
    crc = ~crc;
    for (const auto item : bytes) {
        crc ^= std::to_integer<std::uint8_t>(item);
        for (std::size_t bit = 0; bit < wwdb::bits_per_byte; ++bit) {
            const auto mask = static_cast<std::uint32_t>(
                -static_cast<std::int32_t>(crc & wwdb::crc32_lsb_mask));
            crc = (crc >> 1U) ^ (wwdb::crc32_reflected_polynomial & mask);
        }
    }
    return ~crc;
}

Bytes byte_shuffle(Bytes rows, std::size_t count, std::size_t stride) {
    if (rows.size() != count * stride) {
        fail("cannot byte-shuffle section with inconsistent shape");
    }
    Bytes columns;
    columns.reserve(rows.size());
    for (std::size_t byte_column = 0; byte_column < stride; ++byte_column) {
        for (std::size_t row = 0; row < count; ++row) {
            columns.push_back(rows[(row * stride) + byte_column]);
        }
    }
    return columns;
}

Bytes make_image(std::vector<Section> sections, PackingProfile profile,
                 const std::uint16_t minor_version) {
    const auto section_count = static_cast<std::uint32_t>(sections.size());
    const auto header_bytes =
        wwdb::fixed_header_size + (section_count * wwdb::directory_entry_size);

    std::uint64_t file_size = header_bytes;
    std::uint32_t payload_crc = wwdb::crc32_initial_value;
    for (const auto &section : sections) {
        file_size += section.data.size();
        payload_crc = crc32(section.data, payload_crc);
    }

    Bytes output;
    output.reserve(static_cast<std::size_t>(file_size));
    for (const auto byte : wwdb::magic) {
        append_u8(output, byte);
    }
    append_u16_le(output, wwdb::major_version);
    append_u16_le(output, minor_version);
    append_u32_le(output, wwdb::fixed_header_size);
    append_u32_le(output, section_count);
    append_u32_le(output, std::to_underlying(profile));
    append_u64_le(output, file_size);
    append_u32_le(output, payload_crc);
    append_u32_le(output, wwdb::reserved_value);

    std::uint64_t offset = header_bytes;
    for (const auto &section : sections) {
        append_u32_le(output, std::to_underlying(section.type));
        append_u32_le(output, section.flags);
        append_u64_le(output, offset);
        append_u64_le(output, section.data.size());
        append_u32_le(output, section.count);
        append_u32_le(output, section.stride);
        offset += section.data.size();
    }

    for (auto &section : sections) {
        output.insert(output.end(), section.data.begin(), section.data.end());
    }

    if (output.size() != file_size) {
        fail("internal file-size mismatch");
    }
    return output;
}

} // namespace

int main(int argc, char **argv) try {
    if (argc < required_argument_count || argc > maximum_argument_count) {
        std::cerr << "usage: wwdb_poc_pack REPOSITORY_ROOT OUTPUT.wwdb "
                     "[simple|dense|columnar|search-only] "
                     "[--legacy-stem-order]\n";
        return 2;
    }

    const std::filesystem::path root = argv[1];
    const std::filesystem::path output_path = argv[2];
    PackingProfile profile = PackingProfile::simple;
    if (argc >= 4) {
        const std::string_view requested_profile = argv[3];
        if (requested_profile == "dense") {
            profile = PackingProfile::dense;
        } else if (requested_profile == "columnar") {
            profile = PackingProfile::columnar;
        } else if (requested_profile == "search-only") {
            profile = PackingProfile::search_only;
        } else if (requested_profile != "simple") {
            fail("unknown packing profile: " + std::string(requested_profile));
        }
    }
    bool persist_stem_index{true};
    if (argc == maximum_argument_count) {
        const std::string_view option = argv[4];
        if (option == "--legacy-stem-order") {
            persist_stem_index = false;
        } else {
            fail("unknown packing option: " + std::string{option});
        }
    }

    const auto dictionary = read_file(root / "DICTFILE.GEN");
    const auto stems = read_file(root / "STEMFILE.GEN");
    const auto inflections = read_file(root / "INFLECTS.SEC");
    const auto addons = read_addons(root / "ADDONS.LAT");
    const auto packon_requirements =
        read_packon_requirements(root / "PACKON_REQUIREMENTS.LAT");
    const auto uniques = read_uniques(root / "UNIQUES.LAT");
    const auto rewrites = read_rewrites(root / "REWRITES.LAT");
    const auto quantities = read_quantities(root / "QUANTITIES.LAT");
    const auto suffix_policies =
        read_suffix_policies(root / "ADDON_POLICIES.LAT");
    auto morphological_notices =
        read_morphological_notices(root / morphological_notices_file);
    const auto compiled_lexemes = read_compiled_lexemes(root / "LEXEMES.LAT");

    if (dictionary.size() % dictionary_record_size != 0 ||
        stems.size() % stem_record_size != 0 ||
        inflections.size() != inflection_record_size * inflections_per_section *
                                  inflection_section_count) {
        fail("unexpected legacy file size");
    }

    const auto legacy_lexeme_count = dictionary.size() / dictionary_record_size;
    for (const auto &notice : morphological_notices) {
        if (notice.dictionary_entry > legacy_lexeme_count) {
            fail("morphological notice dictionary entry is outside "
                 "DICTFILE.GEN");
        }
        const auto record = std::span{dictionary}.subspan(
            (notice.dictionary_entry - 1U) * dictionary_record_size,
            dictionary_record_size);
        if (byte_at(record, dictionary_part_of_speech_offset) !=
            legacy_verb_part_of_speech) {
            fail("morphological notice target is not a verb");
        }
        if (notice.trigger == semideponent_passive_present_trigger &&
            byte_at(record, dictionary_class_attribute_offset) !=
                legacy_semideponent_kind) {
            fail("semideponent notice target is not a semideponent verb");
        }
    }
    std::ranges::sort(morphological_notices);
    if (std::ranges::adjacent_find(morphological_notices) !=
        morphological_notices.end()) {
        fail("duplicate morphological notice");
    }
    struct PackedMorphologicalNotice final {
        std::uint16_t dictionary_entry{};
        std::uint8_t trigger{};
        std::uint8_t notice_bits{};
    };
    std::vector<PackedMorphologicalNotice> packed_morphological_notices;
    packed_morphological_notices.reserve(morphological_notices.size());
    for (const auto &notice : morphological_notices) {
        const auto bit =
            static_cast<std::uint8_t>(std::uint8_t{1U} << notice.notice);
        if (!packed_morphological_notices.empty() &&
            packed_morphological_notices.back().dictionary_entry ==
                notice.dictionary_entry &&
            packed_morphological_notices.back().trigger == notice.trigger) {
            packed_morphological_notices.back().notice_bits =
                static_cast<std::uint8_t>(
                    packed_morphological_notices.back().notice_bits | bit);
            continue;
        }
        packed_morphological_notices.push_back(
            {notice.dictionary_entry, notice.trigger, bit});
    }
    if (packon_requirements.size() > legacy_lexeme_count) {
        fail("too many typed packon requirements");
    }
    for (const auto &[entry, fix] : packon_requirements) {
        static_cast<void>(fix);
        if (entry == 0U || entry > legacy_lexeme_count ||
            byte_at(std::span{dictionary}.subspan((entry - 1U) *
                                                      dictionary_record_size,
                                                  dictionary_record_size),
                    dictionary_part_of_speech_offset) !=
                legacy_pack_part_of_speech) {
            fail("typed packon requirement does not identify a PACK lexeme");
        }
    }
    const auto imported_stem_reference_count = std::ranges::fold_left(
        compiled_lexemes, std::size_t{0},
        [](const std::size_t count, const CompiledLexeme &lexeme) {
            return count + static_cast<std::size_t>(std::ranges::count_if(
                               lexeme.stems, [](const std::string &stem) {
                                   return !stem.empty();
                               }));
        });
    const auto legacy_stem_reference_count = stems.size() / stem_record_size;
    const auto lexeme_count = legacy_lexeme_count + compiled_lexemes.size();
    const auto stem_reference_count =
        legacy_stem_reference_count + imported_stem_reference_count;
    if (lexeme_count + uniques.size() >
            std::numeric_limits<std::uint16_t>::max() + 1ULL ||
        stem_reference_count > std::numeric_limits<std::uint16_t>::max()) {
        fail("WWDB u16 lexeme/reference capacity exceeded after LEXEMES.LAT "
             "import");
    }

    StringPool stem_pool;
    StringPool meaning_pool;
    StringPool ending_pool;
    StringPool suffix_string_pool;
    StringPool suffix_meaning_pool;
    StringPool prefix_string_pool;
    StringPool prefix_meaning_pool;
    StringPool tackon_string_pool;
    StringPool tackon_meaning_pool;
    StringPool rewrite_string_pool;
    StringPool rewrite_meaning_pool;
    Bytes lexeme_records;
    const bool use_dense_records = profile != PackingProfile::simple;
    const bool use_byte_columns = profile == PackingProfile::columnar ||
                                  profile == PackingProfile::search_only;
    const bool include_meanings = profile != PackingProfile::search_only;
    const std::size_t lexeme_stride =
        !use_dense_records ? legacy_simple_lexeme_stride
                           : (include_meanings ? wwdb::full_lexeme_stride
                                               : wwdb::search_lexeme_stride);
    lexeme_records.reserve(lexeme_count * lexeme_stride);
    const auto structural_signature =
        [](const auto &stem_spellings, const std::uint8_t part,
           const std::uint8_t paradigm_value, const std::uint8_t attribute_0,
           const std::uint8_t attribute_1, const std::uint16_t numeric_value) {
            std::string signature;
            for (const auto &stem : stem_spellings) {
                signature.append(stem);
                signature.push_back('\x1f');
            }
            signature.append(std::to_string(part));
            signature.push_back(':');
            signature.append(std::to_string(paradigm_value));
            signature.push_back(':');
            signature.append(std::to_string(attribute_0));
            signature.push_back(':');
            signature.append(std::to_string(attribute_1));
            signature.push_back(':');
            signature.append(std::to_string(numeric_value));
            return signature;
        };
    std::unordered_map<std::string, std::size_t> lexical_structures;
    lexical_structures.reserve(lexeme_count);

    for (std::size_t index = 0; index < legacy_lexeme_count; ++index) {
        const auto record = std::span{dictionary}.subspan(
            index * dictionary_record_size, dictionary_record_size);

        std::array<std::string, wwdb::lexical_slot_count> stem_spellings;
        std::array<std::uint16_t, wwdb::lexical_slot_count> stem_ids{};
        for (std::size_t stem_index = 0; stem_index < stem_ids.size();
             ++stem_index) {
            stem_spellings[stem_index] =
                fixed_string(record, stem_index * dictionary_stem_width,
                             dictionary_stem_width);
            stem_ids[stem_index] = stem_pool.intern(stem_spellings[stem_index]);
        }
        const auto meaning = fixed_string(record, dictionary_meaning_offset,
                                          legacy_meaning_width);
        std::uint16_t meaning_id = 0;
        if (include_meanings) {
            meaning_id = meaning_pool.intern(meaning);
        }

        const auto pofs = byte_at(record, dictionary_part_of_speech_offset);
        if (pofs > std::to_underlying(words::PartOfSpeech::suffix)) {
            fail("dictionary part-of-speech outside legacy range");
        }
        const auto which = has_paradigm(pofs)
                               ? read_u32_le(record, dictionary_paradigm_offset)
                               : 0;
        const auto variant =
            has_paradigm(pofs) ? read_u32_le(record, dictionary_variant_offset)
                               : 0;
        const auto paradigm = pack_paradigm(which, variant);

        std::uint8_t attribute_0 = 0;
        std::uint8_t attribute_1 = 0;
        std::uint16_t numeric_value = 0;
        switch (pofs) {
        case pos_noun: // noun
            attribute_0 =
                byte_at(record, dictionary_class_attribute_offset); // gender
            attribute_1 = byte_at(
                record, dictionary_second_attribute_offset); // noun kind
            break;
        case pos_pronoun:   // pronoun
        case pos_pack:      // pack
        case pos_adjective: // adjective
        case pos_verb:      // verb
            attribute_0 = byte_at(record, dictionary_class_attribute_offset);
            break;
        case pos_numeral: // numeral
            attribute_0 = byte_at(record, dictionary_class_attribute_offset);
            numeric_value = static_cast<std::uint16_t>(
                read_u32_le(record, dictionary_numeric_value_offset));
            break;
        case pos_adverb:      // adverb
        case pos_preposition: // preposition
            attribute_0 = byte_at(record, dictionary_paradigm_offset);
            break;
        default:
            break;
        }
        const auto translation = pack_translation(record);
        lexical_structures.try_emplace(
            structural_signature(stem_spellings, pofs, paradigm, attribute_0,
                                 attribute_1, numeric_value),
            index);
        if (!use_dense_records) {
            for (const auto stem_id : stem_ids) {
                append_u16_le(lexeme_records, stem_id);
            }
            if (include_meanings) {
                append_u16_le(lexeme_records, meaning_id);
            }
            append_u8(lexeme_records, pofs);
            append_u8(lexeme_records, paradigm);
            append_u8(lexeme_records, attribute_0);
            append_u8(lexeme_records, attribute_1);
            append_u16_le(lexeme_records, numeric_value);
            append_u24_le(lexeme_records, translation);
        } else {
            for (const auto stem_id : stem_ids) {
                append_u16_le(lexeme_records, stem_id);
            }
            if (include_meanings) {
                append_u16_le(lexeme_records, meaning_id);
            }

            std::uint64_t class_payload = 0;
            switch (pofs) {
            case pos_noun: // noun: gender:3 | noun kind:4
                if (attribute_0 > wwdb::three_bit_mask ||
                    attribute_1 > maximum_encoded_noun_kind) {
                    fail("noun attribute outside dense profile");
                }
                class_payload = static_cast<std::uint64_t>(attribute_0) |
                                (static_cast<std::uint64_t>(attribute_1)
                                 << wwdb::noun_kind_shift);
                break;
            case pos_pronoun:   // pronoun kind
            case pos_adjective: // adjective comparison
            case pos_verb:      // verb kind
                if (attribute_0 > maximum_simple_class) {
                    fail("lexeme attribute outside dense profile");
                }
                class_payload = attribute_0;
                break;
            case pos_pack: { // pack kind + required packon addon
                if (attribute_0 > maximum_simple_class) {
                    fail("pack attribute outside dense profile");
                }
                const auto requirement = packon_requirements.find(
                    static_cast<std::uint32_t>(index + 1U));
                if (requirement == packon_requirements.end()) {
                    fail("PACK lexeme has no typed packon requirement");
                }
                std::uint16_t packon_plus_one = 0;
                for (const auto &tackon : addons.tackons) {
                    if (!tackon.packon) {
                        continue;
                    }
                    if (tackon.fix != requirement->second) {
                        continue;
                    }
                    if (packon_plus_one != 0U ||
                        tackon.addon_id >= maximum_encoded_packon_plus_one) {
                        fail("pack lexeme has ambiguous/out-of-range packon");
                    }
                    packon_plus_one =
                        static_cast<std::uint16_t>(tackon.addon_id + 1U);
                }
                if (packon_plus_one == 0U) {
                    fail("typed packon requirement has no matching addon");
                }
                class_payload = static_cast<std::uint64_t>(attribute_0) |
                                (static_cast<std::uint64_t>(packon_plus_one)
                                 << wwdb::pronoun_packon_plus_one_shift);
                break;
            }
            case pos_numeral: // numeral sort:3 | numeric value:10
                if (attribute_0 > wwdb::three_bit_mask ||
                    numeric_value > maximum_numeral_value) {
                    fail("numeral payload outside dense profile");
                }
                class_payload = static_cast<std::uint64_t>(attribute_0) |
                                (static_cast<std::uint64_t>(numeric_value)
                                 << wwdb::numeral_value_shift);
                break;
            case pos_adverb:      // adverb comparison
            case pos_preposition: // preposition case
                if (attribute_0 > maximum_simple_class) {
                    fail("lexeme attribute outside dense profile");
                }
                class_payload = attribute_0;
                break;
            default:
                break;
            }

            const std::uint64_t metadata =
                static_cast<std::uint64_t>(pofs) |
                (static_cast<std::uint64_t>(paradigm) << wwdb::paradigm_shift) |
                (static_cast<std::uint64_t>(translation)
                 << wwdb::translation_shift) |
                (class_payload << wwdb::lexeme_class_payload_shift);
            append_u48_le(lexeme_records, metadata);
        }
    }

    for (std::size_t imported_index = 0;
         imported_index < compiled_lexemes.size(); ++imported_index) {
        const auto &lexeme = compiled_lexemes[imported_index];
        std::uint8_t attribute_0 = 0;
        std::uint8_t attribute_1 = 0;
        if (lexeme.part_of_speech == pos_noun) {
            attribute_0 = static_cast<std::uint8_t>(lexeme.class_payload &
                                                    wwdb::three_bit_mask);
            attribute_1 = static_cast<std::uint8_t>(lexeme.class_payload >>
                                                    wwdb::noun_kind_shift);
        } else if (lexeme.part_of_speech == pos_numeral) {
            attribute_0 = static_cast<std::uint8_t>(lexeme.class_payload &
                                                    wwdb::three_bit_mask);
        } else if (lexeme.part_of_speech == pos_pronoun ||
                   lexeme.part_of_speech == pos_adjective ||
                   lexeme.part_of_speech == pos_adverb ||
                   lexeme.part_of_speech == pos_verb ||
                   lexeme.part_of_speech == pos_preposition) {
            attribute_0 = static_cast<std::uint8_t>(lexeme.class_payload);
        }
        const auto signature = structural_signature(
            lexeme.stems, lexeme.part_of_speech, lexeme.paradigm, attribute_0,
            attribute_1, lexeme.numeric_value);
        const auto [collision, inserted] = lexical_structures.try_emplace(
            signature, legacy_lexeme_count + imported_index);
        if (!inserted) {
            fail("LEXEMES.LAT decision " + lexeme.decision_id +
                 " collides with lexical entry " +
                 std::to_string(collision->second + 1U));
        }

        std::array<std::uint16_t, 4> stem_ids{};
        std::ranges::transform(
            lexeme.stems, stem_ids.begin(),
            [&](const std::string &stem) { return stem_pool.intern(stem); });
        std::uint16_t meaning_id = 0;
        if (include_meanings) {
            meaning_id = meaning_pool.intern(lexeme.meaning);
        }
        for (const auto stem_id : stem_ids) {
            append_u16_le(lexeme_records, stem_id);
        }
        if (include_meanings) {
            append_u16_le(lexeme_records, meaning_id);
        }
        if (!use_dense_records) {
            append_u8(lexeme_records, lexeme.part_of_speech);
            append_u8(lexeme_records, lexeme.paradigm);
            append_u8(lexeme_records, attribute_0);
            append_u8(lexeme_records, attribute_1);
            append_u16_le(lexeme_records, lexeme.numeric_value);
            append_u24_le(lexeme_records, lexeme.translation);
        } else {
            const std::uint64_t metadata =
                static_cast<std::uint64_t>(lexeme.part_of_speech) |
                (static_cast<std::uint64_t>(lexeme.paradigm)
                 << wwdb::paradigm_shift) |
                (static_cast<std::uint64_t>(lexeme.translation)
                 << wwdb::translation_shift) |
                (static_cast<std::uint64_t>(lexeme.class_payload)
                 << wwdb::lexeme_class_payload_shift);
            append_u48_le(lexeme_records, metadata);
        }
    }

    Bytes stem_reference_records;
    const std::size_t stem_reference_stride =
        use_dense_records ? wwdb::stem_reference_stride : 5U;
    stem_reference_records.reserve(stem_reference_count *
                                   stem_reference_stride);
    struct PendingStemReference final {
        std::string stem;
        std::uint16_t lexeme_id{};
        std::uint8_t lexical_slot{};
        std::uint8_t stem_key{};
    };
    std::array<std::vector<PendingStemReference>, wwdb::stem_bucket_count>
        stem_buckets;
    std::size_t previous_bucket = 0;
    std::string previous_stem;

    for (std::size_t index = 0; index < legacy_stem_reference_count; ++index) {
        const auto record = std::span{stems}.subspan(index * stem_record_size,
                                                     stem_record_size);
        const auto stem = fixed_string(record, 0, dictionary_stem_width);
        const auto bucket = stem_bucket(stem);
        if (index != 0 && bucket < previous_bucket) {
            fail("STEMFILE is not monotonic in the proposed prefix order at " +
                 std::to_string(index) + ": [" + previous_stem + "] bucket " +
                 std::to_string(previous_bucket) + " -> [" + stem +
                 "] bucket " + std::to_string(bucket));
        }
        previous_bucket = bucket;
        previous_stem = stem;

        const auto mnpc = read_u64_le(record, stem_dictionary_entry_offset);
        if (mnpc == 0 || mnpc > legacy_lexeme_count) {
            fail("STEMFILE MNPC outside dictionary range");
        }
        const auto key = read_u32_le(record, stem_key_offset);
        if (key > maximum_stem_key) {
            fail("STEMFILE key outside observed 0..4 range");
        }

        const auto lexeme_id = static_cast<std::uint16_t>(mnpc - 1);
        const auto dictionary_record = std::span{dictionary}.subspan(
            static_cast<std::size_t>(lexeme_id) * dictionary_record_size,
            dictionary_record_size);
        std::size_t lexical_slot = wwdb::lexical_slot_count;
        if (key >= 1 && key <= maximum_stem_key &&
            fixed_string(
                dictionary_record,
                static_cast<std::size_t>((key - 1) * dictionary_stem_width),
                dictionary_stem_width) == stem) {
            lexical_slot = key - 1;
        } else {
            for (std::size_t candidate = 0;
                 candidate < wwdb::lexical_slot_count; ++candidate) {
                if (fixed_string(dictionary_record,
                                 candidate * dictionary_stem_width,
                                 dictionary_stem_width) == stem) {
                    lexical_slot = candidate;
                    break;
                }
            }
        }
        if (lexical_slot == wwdb::lexical_slot_count) {
            fail("STEMFILE stem is absent from referenced lexeme: " + stem);
        }
        stem_buckets.at(bucket).push_back(PendingStemReference{
            .stem = stem,
            .lexeme_id = lexeme_id,
            .lexical_slot = static_cast<std::uint8_t>(lexical_slot),
            .stem_key = static_cast<std::uint8_t>(key)});
    }

    for (std::size_t imported_index = 0;
         imported_index < compiled_lexemes.size(); ++imported_index) {
        const auto lexeme_id_value = legacy_lexeme_count + imported_index;
        if (lexeme_id_value > std::numeric_limits<std::uint16_t>::max()) {
            fail("LEXEMES.LAT lexeme ID exceeds u16");
        }
        const auto lexeme_id = static_cast<std::uint16_t>(lexeme_id_value);
        const auto &lexeme = compiled_lexemes[imported_index];
        for (std::size_t lexical_slot = 0; lexical_slot < lexeme.stems.size();
             ++lexical_slot) {
            const auto &stem = lexeme.stems[lexical_slot];
            if (stem.empty()) {
                continue;
            }
            stem_buckets.at(stem_bucket(stem))
                .push_back(PendingStemReference{
                    .stem = stem,
                    .lexeme_id = lexeme_id,
                    .lexical_slot = static_cast<std::uint8_t>(lexical_slot),
                    .stem_key = static_cast<std::uint8_t>(lexical_slot + 1U)});
        }
    }

    for (auto &bucket : stem_buckets) {
        if (persist_stem_index) {
            std::ranges::sort(bucket, [](const PendingStemReference &left,
                                         const PendingStemReference &right) {
                const auto order =
                    canonical_stem_compare(left.stem, right.stem);
                if (!std::is_eq(order)) {
                    return std::is_lt(order);
                }
                return std::tuple{left.lexeme_id, left.lexical_slot,
                                  left.stem_key} <
                       std::tuple{right.lexeme_id, right.lexical_slot,
                                  right.stem_key};
            });
        } else {
            const auto imported = std::ranges::find_if(
                bucket, [&](const PendingStemReference &reference) {
                    return reference.lexeme_id >= legacy_lexeme_count;
                });
            std::ranges::sort(imported, bucket.end(), {},
                              &PendingStemReference::stem);
        }
        for (const auto &reference : bucket) {
            if (!use_dense_records) {
                append_u16_le(stem_reference_records,
                              stem_pool.id_of(reference.stem));
                append_u16_le(stem_reference_records, reference.lexeme_id);
                append_u8(stem_reference_records, reference.stem_key);
                continue;
            }
            const auto packed_reference =
                static_cast<std::uint32_t>(reference.lexeme_id) |
                (static_cast<std::uint32_t>(reference.lexical_slot)
                 << wwdb::stem_reference_slot_shift) |
                (static_cast<std::uint32_t>(reference.stem_key)
                 << wwdb::stem_reference_key_shift);
            append_u24_le(stem_reference_records, packed_reference);
        }
    }

    Bytes stem_boundaries;
    std::uint32_t stem_boundary = 0;
    append_u16_le(stem_boundaries, 0);
    for (const auto &bucket : stem_buckets) {
        stem_boundary += static_cast<std::uint32_t>(bucket.size());
        if (stem_boundary > std::numeric_limits<std::uint16_t>::max()) {
            fail("stem prefix boundary exceeds u16");
        }
        append_u16_le(stem_boundaries,
                      static_cast<std::uint16_t>(stem_boundary));
    }
    if (stem_boundary != stem_reference_count) {
        fail("stem prefix boundaries do not cover STEMFILE");
    }

    Bytes inflection_records;
    Bytes inflection_boundaries;
    std::vector<std::string> inflection_endings;
    std::uint16_t dense_inflection_count = 0;
    append_u16_le(inflection_boundaries, 0);

    for (std::size_t section = 0; section < inflection_section_count;
         ++section) {
        for (std::size_t item = 0; item < inflections_per_section; ++item) {
            const auto record = std::span{inflections}.subspan(
                (section * inflections_per_section + item) *
                    inflection_record_size,
                inflection_record_size);
            const auto pofs = byte_at(record, inflection_part_of_speech_offset);
            if (pofs == 0) {
                continue;
            }
            if (pofs > std::to_underlying(words::PartOfSpeech::suffix)) {
                fail("inflection part-of-speech outside legacy range");
            }

            const auto which =
                has_paradigm(pofs)
                    ? read_u32_le(record, inflection_paradigm_offset)
                    : 0;
            const auto variant =
                has_paradigm(pofs)
                    ? read_u32_le(record, inflection_variant_offset)
                    : 0;
            const auto stem_key =
                read_u32_le(record, inflection_stem_key_offset);
            const auto ending_size =
                read_u32_le(record, inflection_ending_size_offset);
            if (stem_key < 1 || stem_key > maximum_stem_key ||
                ending_size > wwdb::maximum_ending_size) {
                fail("inflection key/ending size outside compact range");
            }

            const auto ending =
                fixed_string(record, inflection_ending_offset, ending_size);
            const auto ending_id = ending_pool.intern(ending);
            if (ending_id > wwdb::ending_id_mask) {
                fail("PoC 9-bit ending ID capacity exceeded");
            }

            const auto age = byte_at(record, inflection_age_offset);
            const auto frequency = byte_at(record, inflection_frequency_offset);
            if (age > maximum_lexical_age ||
                frequency > maximum_rule_frequency) {
                fail("inflection age/frequency outside legacy range");
            }

            const auto paradigm = pack_paradigm(which, variant);
            const auto morphology = pack_inflection_morphology(record, pofs);
            if (!use_dense_records) {
                append_u8(inflection_records, pofs);
                append_u8(inflection_records, paradigm);
                append_u16_le(inflection_records, morphology);
                append_u16_le(
                    inflection_records,
                    static_cast<std::uint16_t>(
                        (static_cast<std::uint32_t>(ending_id) << 2U) |
                        (stem_key - 1U)));
                append_u8(inflection_records,
                          static_cast<std::uint8_t>((age << 4U) | frequency));
                append_u8(inflection_records, 0); // version-1 flags
            } else {
                const std::uint64_t packed_inflection =
                    static_cast<std::uint64_t>(pofs) |
                    (static_cast<std::uint64_t>(paradigm)
                     << wwdb::paradigm_shift) |
                    (static_cast<std::uint64_t>(morphology)
                     << wwdb::morphology_shift) |
                    (static_cast<std::uint64_t>(ending_id)
                     << wwdb::ending_id_shift) |
                    (static_cast<std::uint64_t>(stem_key -
                                                wwdb::inflection_stem_key_bias)
                     << wwdb::inflection_stem_key_shift) |
                    (static_cast<std::uint64_t>(age)
                     << wwdb::inflection_age_shift) |
                    (static_cast<std::uint64_t>(frequency)
                     << wwdb::inflection_frequency_shift);
                append_u48_le(inflection_records, packed_inflection);
            }
            inflection_endings.push_back(ending);
            ++dense_inflection_count;
        }
        append_u16_le(inflection_boundaries, dense_inflection_count);
    }

    std::vector<std::uint16_t> packed_inflection_quantities(
        dense_inflection_count);
    for (const auto &quantity : quantities.inflections) {
        if (quantity.rule_id >= dense_inflection_count ||
            quantity.known == 0U ||
            quantity.known > wwdb::inflection_quantity_value_mask ||
            quantity.long_vowel > wwdb::inflection_quantity_value_mask ||
            (quantity.long_vowel & ~quantity.known) != 0U) {
            fail("invalid inflection quantity in QUANTITIES.LAT");
        }
        const auto &ending = inflection_endings.at(quantity.rule_id);
        const auto ending_size = ending.size();
        const auto valid_bits =
            ending_size == 0U ? 0U : (std::uint32_t{1U} << ending_size) - 1U;
        if (((quantity.known | quantity.long_vowel) & ~valid_bits) != 0U ||
            !quantity_positions_are_vowels(ending, quantity.known) ||
            packed_inflection_quantities[quantity.rule_id] != 0U) {
            fail("duplicate or out-of-range inflection quantity");
        }
        packed_inflection_quantities[quantity.rule_id] =
            static_cast<std::uint16_t>(
                quantity.known |
                (quantity.long_vowel << wwdb::maximum_ending_size));
    }
    Bytes inflection_quantity_records;
    inflection_quantity_records.reserve(packed_inflection_quantities.size() *
                                        wwdb::inflection_quantity_stride);
    for (const auto quantity : packed_inflection_quantities) {
        append_u16_le(inflection_quantity_records, quantity);
    }

    struct PackedStemQuantity final {
        std::uint32_t key{};
        std::uint32_t known{};
        std::uint32_t long_vowel{};
    };
    std::vector<PackedStemQuantity> packed_stem_quantities;
    packed_stem_quantities.reserve(quantities.stems.size());
    for (const auto &quantity : quantities.stems) {
        const auto lexeme_id =
            static_cast<std::size_t>(quantity.dictionary_entry - 1U);
        const auto lexical_slot =
            static_cast<std::size_t>(quantity.lexical_slot - 1U);
        if (lexeme_id >= lexeme_count) {
            fail("stem quantity dictionary entry is outside DICTFILE.GEN");
        }
        const auto record = std::span{dictionary}.subspan(
            lexeme_id * dictionary_record_size, dictionary_record_size);
        const auto stem =
            fixed_string(record, lexical_slot * wwdb::maximum_stem_size,
                         wwdb::maximum_stem_size);
        const auto valid_bits =
            stem.empty() ? 0U : (std::uint32_t{1U} << stem.size()) - 1U;
        if (stem.empty() || quantity.known == 0U ||
            quantity.known > wwdb::stem_quantity_value_mask ||
            quantity.long_vowel > wwdb::stem_quantity_value_mask ||
            (quantity.long_vowel & ~quantity.known) != 0U ||
            ((quantity.known | quantity.long_vowel) & ~valid_bits) != 0U ||
            !quantity_positions_are_vowels(stem, quantity.known)) {
            fail("invalid stem quantity in QUANTITIES.LAT");
        }
        packed_stem_quantities.push_back({
            static_cast<std::uint32_t>(lexeme_id) |
                (static_cast<std::uint32_t>(lexical_slot)
                 << wwdb::stem_quantity_slot_shift),
            quantity.known,
            quantity.long_vowel,
        });
    }
    std::ranges::sort(packed_stem_quantities, {}, &PackedStemQuantity::key);
    if (std::ranges::adjacent_find(
            packed_stem_quantities, std::ranges::equal_to{},
            &PackedStemQuantity::key) != packed_stem_quantities.end()) {
        fail("duplicate stem quantity in QUANTITIES.LAT");
    }
    Bytes stem_quantity_records;
    stem_quantity_records.reserve(packed_stem_quantities.size() *
                                  wwdb::stem_quantity_stride);
    for (const auto &quantity : packed_stem_quantities) {
        append_u24_le(stem_quantity_records, quantity.key);
        append_u24_le(stem_quantity_records, quantity.known);
        append_u24_le(stem_quantity_records, quantity.long_vowel);
    }

    Bytes morphological_notice_records;
    morphological_notice_records.reserve(packed_morphological_notices.size() *
                                         wwdb::morphological_notice_stride);
    for (const auto &notice : packed_morphological_notices) {
        append_u16_le(morphological_notice_records,
                      static_cast<std::uint16_t>(notice.dictionary_entry - 1U));
        const auto metadata = static_cast<std::uint8_t>(
            notice.trigger |
            static_cast<std::uint8_t>(
                notice.notice_bits << wwdb::morphological_notice_values_shift));
        append_u8(morphological_notice_records, metadata);
    }

    Bytes suffix_records;
    const std::uint32_t suffix_stride = include_meanings
                                            ? wwdb::full_suffix_stride
                                            : wwdb::search_suffix_stride;
    suffix_records.reserve(addons.suffixes.size() * suffix_stride);
    for (const auto &suffix : addons.suffixes) {
        append_u16_le(suffix_records, suffix.addon_id);
        append_u16_le(suffix_records, suffix_string_pool.intern(suffix.fix));
        if (include_meanings) {
            append_u16_le(suffix_records,
                          suffix_meaning_pool.intern(suffix.meaning));
        }
        const std::uint64_t metadata =
            static_cast<std::uint64_t>(suffix.root) |
            (static_cast<std::uint64_t>(suffix.root_key)
             << wwdb::suffix_root_key_shift) |
            (static_cast<std::uint64_t>(suffix.target)
             << wwdb::suffix_target_pos_shift) |
            (static_cast<std::uint64_t>(suffix.target_key)
             << wwdb::suffix_target_key_shift) |
            (static_cast<std::uint64_t>(suffix.paradigm)
             << wwdb::suffix_paradigm_shift) |
            (static_cast<std::uint64_t>(suffix.attribute_0)
             << wwdb::suffix_attribute_shift) |
            (static_cast<std::uint64_t>(suffix.attribute_1)
             << wwdb::suffix_noun_kind_shift) |
            (static_cast<std::uint64_t>(suffix.numeric_value)
             << wwdb::suffix_numeric_value_shift) |
            (static_cast<std::uint64_t>(suffix.connect)
             << wwdb::suffix_connector_shift);
        append_u64_le(suffix_records, metadata);
    }

    Bytes addon_attribute_records;
    std::vector<SuffixAttributeSource> suffix_attributes = quantities.suffixes;
    std::vector<std::uint16_t> policy_ids;
    for (const auto &policy : suffix_policies) {
        const auto found = std::ranges::find(addons.suffixes, policy.addon_id,
                                             &SuffixSource::addon_id);
        if (found == addons.suffixes.end() || found->fix != policy.fix ||
            found->root != policy.root || found->root_key != policy.root_key ||
            found->target != policy.target ||
            found->target_key != policy.target_key ||
            found->attribute_0 != policy.target_degree ||
            found->connect != static_cast<std::uint8_t>(policy.connector) ||
            std::ranges::find(policy_ids, policy.addon_id) !=
                policy_ids.end()) {
            fail("suffix policy is inconsistent with ADDONS.LAT");
        }
        policy_ids.push_back(policy.addon_id);
        const auto attribute =
            std::ranges::find(suffix_attributes, policy.addon_id,
                              &SuffixAttributeSource::addon_id);
        if (attribute == suffix_attributes.end()) {
            suffix_attributes.push_back(
                {policy.addon_id, policy.fix, policy.root, policy.root_key,
                 policy.target, policy.target_key, policy.root_declension,
                 policy.root_variant});
        } else if (attribute->root_declension != policy.root_declension ||
                   attribute->root_variant != policy.root_variant) {
            fail("suffix policy conflicts with quantity evidence");
        }
    }
    std::ranges::sort(policy_ids);
    std::ranges::sort(suffix_attributes, {}, &SuffixAttributeSource::addon_id);
    std::optional<std::uint16_t> previous_addon_id;
    for (const auto &attribute : suffix_attributes) {
        const auto has_policy =
            std::ranges::binary_search(policy_ids, attribute.addon_id);
        if (!has_policy &&
            (attribute.root_declension != 0U || attribute.root_variant != 0U)) {
            fail("suffix quantity requires an addon policy");
        }
        const auto found = std::ranges::find(
            addons.suffixes, attribute.addon_id, &SuffixSource::addon_id);
        if (found == addons.suffixes.end() ||
            (previous_addon_id && *previous_addon_id == attribute.addon_id) ||
            found->fix != attribute.fix || found->root != attribute.root ||
            found->root_key != attribute.root_key ||
            found->target != attribute.target ||
            found->target_key != attribute.target_key ||
            attribute.root_declension > maximum_paradigm_component ||
            attribute.root_variant > maximum_paradigm_component ||
            attribute.known > std::numeric_limits<std::uint16_t>::max() ||
            attribute.long_vowel > std::numeric_limits<std::uint16_t>::max() ||
            (attribute.long_vowel & ~attribute.known) != 0U ||
            (attribute.known >> found->fix.size()) != 0U ||
            !quantity_positions_are_vowels(found->fix, attribute.known)) {
            fail("suffix attribute is inconsistent with ADDONS.LAT");
        }
        append_u16_le(addon_attribute_records, attribute.addon_id);
        const auto policy_flags =
            has_policy ? wwdb::addon_attribute_coexists_with_regular : 0U;
        append_u8(
            addon_attribute_records,
            static_cast<std::uint8_t>(
                std::to_underlying(words::AddonKind::suffix) | policy_flags));
        append_u8(
            addon_attribute_records,
            pack_paradigm(attribute.root_declension, attribute.root_variant));
        append_u16_le(addon_attribute_records,
                      static_cast<std::uint16_t>(attribute.known));
        append_u16_le(addon_attribute_records,
                      static_cast<std::uint16_t>(attribute.long_vowel));
        previous_addon_id = attribute.addon_id;
    }

    Bytes prefix_records;
    const std::uint32_t prefix_stride = include_meanings
                                            ? wwdb::full_prefix_stride
                                            : wwdb::search_prefix_stride;
    prefix_records.reserve(addons.prefixes.size() * prefix_stride);
    for (const auto &prefix : addons.prefixes) {
        append_u16_le(prefix_records, prefix.addon_id);
        append_u16_le(prefix_records, prefix_string_pool.intern(prefix.fix));
        if (include_meanings) {
            append_u16_le(prefix_records,
                          prefix_meaning_pool.intern(prefix.meaning));
        }
        const auto metadata = static_cast<std::uint16_t>(
            static_cast<std::uint16_t>(prefix.root) |
            (static_cast<std::uint16_t>(prefix.target)
             << wwdb::prefix_target_shift) |
            (static_cast<std::uint16_t>(prefix.connect)
             << wwdb::prefix_connector_shift));
        append_u16_le(prefix_records, metadata);
    }

    Bytes tackon_records;
    const std::uint32_t tackon_stride = include_meanings
                                            ? wwdb::full_tackon_stride
                                            : wwdb::search_tackon_stride;
    tackon_records.reserve(addons.tackons.size() * tackon_stride);
    for (const auto &tackon : addons.tackons) {
        append_u16_le(tackon_records, tackon.addon_id);
        append_u16_le(tackon_records, tackon_string_pool.intern(tackon.fix));
        if (include_meanings) {
            append_u16_le(tackon_records,
                          tackon_meaning_pool.intern(tackon.meaning));
        }
        const std::uint32_t metadata =
            static_cast<std::uint32_t>(tackon.base) |
            (static_cast<std::uint32_t>(tackon.paradigm)
             << wwdb::paradigm_shift) |
            (static_cast<std::uint32_t>(tackon.attribute_0)
             << wwdb::tackon_attribute_shift) |
            (static_cast<std::uint32_t>(tackon.attribute_1)
             << wwdb::tackon_noun_kind_shift) |
            (static_cast<std::uint32_t>(tackon.packon)
             << wwdb::tackon_packon_shift) |
            (static_cast<std::uint32_t>(tackon.enclitic)
             << wwdb::tackon_enclitic_shift);
        append_u32_le(tackon_records, metadata);
    }

    Bytes unique_records;
    const std::uint32_t unique_stride = include_meanings
                                            ? wwdb::full_unique_stride
                                            : wwdb::search_unique_stride;
    unique_records.reserve(uniques.size() * unique_stride);
    for (const auto &unique : uniques) {
        append_u16_le(unique_records, stem_pool.intern(unique.surface));
        if (include_meanings) {
            append_u16_le(unique_records, meaning_pool.intern(unique.meaning));
        }
        // A unique already is a complete parse. Keeping its morphology beside
        // the lexical metadata avoids manufacturing an inflection rule that
        // never existed in the source data.
        const std::uint64_t metadata =
            static_cast<std::uint64_t>(unique.part_of_speech) |
            (static_cast<std::uint64_t>(unique.paradigm)
             << wwdb::paradigm_shift) |
            (static_cast<std::uint64_t>(unique.morphology)
             << wwdb::morphology_shift) |
            (static_cast<std::uint64_t>(unique.translation)
             << wwdb::unique_translation_shift);
        append_u64_le(unique_records, metadata);
    }

    Bytes rewrite_records;
    const std::uint32_t rewrite_stride = include_meanings
                                             ? wwdb::full_rewrite_stride
                                             : wwdb::search_rewrite_stride;
    rewrite_records.reserve(rewrites.size() * rewrite_stride);
    for (std::size_t ordinal = 0; ordinal < rewrites.size(); ++ordinal) {
        const auto &rewrite = rewrites[ordinal];
        if (ordinal > std::numeric_limits<std::uint16_t>::max()) {
            fail("PoC u16 rewrite ID capacity exceeded");
        }
        append_u16_le(rewrite_records, static_cast<std::uint16_t>(ordinal));
        append_u16_le(rewrite_records,
                      rewrite_string_pool.intern(rewrite.before));
        append_u16_le(rewrite_records,
                      rewrite_string_pool.intern(rewrite.after));
        append_u16_le(rewrite_records,
                      rewrite_string_pool.intern(rewrite.name));
        if (include_meanings) {
            append_u16_le(rewrite_records,
                          rewrite_meaning_pool.intern(rewrite.meaning));
        }
        const std::uint32_t metadata =
            static_cast<std::uint32_t>(rewrite.kind) |
            (static_cast<std::uint32_t>(rewrite.scope)
             << wwdb::rewrite_scope_shift) |
            (static_cast<std::uint32_t>(rewrite.priority)
             << wwdb::rewrite_priority_shift) |
            (static_cast<std::uint32_t>(rewrite.scan_reverse)
             << wwdb::rewrite_scan_reverse_shift) |
            (static_cast<std::uint32_t>(rewrite.required_part)
             << wwdb::rewrite_required_pos_shift) |
            (static_cast<std::uint32_t>(rewrite.required_stem_key)
             << wwdb::rewrite_stem_key_shift) |
            (static_cast<std::uint32_t>(rewrite.minimum_before)
             << wwdb::rewrite_minimum_before_shift) |
            (static_cast<std::uint32_t>(rewrite.minimum_after)
             << wwdb::rewrite_minimum_after_shift) |
            (static_cast<std::uint32_t>(rewrite.medieval)
             << wwdb::rewrite_medieval_shift);
        append_u32_le(rewrite_records, metadata);
        // Integer promotions apply to each shift and OR.  Narrow only after
        // composing the validated seven-bit payload so conversion warnings do
        // not hide an accidental future expansion of the wire field.
        const auto behavior = static_cast<std::uint16_t>(
            static_cast<std::uint16_t>(rewrite.operation) |
            (static_cast<std::uint16_t>(rewrite.stage)
             << wwdb::rewrite_stage_shift) |
            (static_cast<std::uint16_t>(rewrite.constraint)
             << wwdb::rewrite_constraint_shift));
        append_u16_le(rewrite_records, behavior);
    }

    std::vector<Section> sections;
    sections.push_back({SectionType::stem_strings, wwdb::section_flag_pool,
                        stem_pool.size(), wwdb::variable_stride,
                        stem_pool.encode()});
    if (include_meanings) {
        sections.push_back({SectionType::meaning_strings,
                            wwdb::section_flag_pool, meaning_pool.size(),
                            wwdb::variable_stride, meaning_pool.encode()});
    }
    sections.push_back({SectionType::ending_strings, wwdb::section_flag_pool,
                        ending_pool.size(), wwdb::variable_stride,
                        ending_pool.encode()});
    if (use_byte_columns) {
        lexeme_records = byte_shuffle(std::move(lexeme_records), lexeme_count,
                                      lexeme_stride);
        stem_reference_records =
            byte_shuffle(std::move(stem_reference_records),
                         stem_reference_count, stem_reference_stride);
        inflection_records =
            byte_shuffle(std::move(inflection_records), dense_inflection_count,
                         wwdb::inflection_stride);
    }

    const std::uint32_t record_flags = use_byte_columns
                                           ? wwdb::section_flag_columnar
                                           : wwdb::section_flag_row_major;
    sections.push_back({SectionType::lexemes, record_flags,
                        static_cast<std::uint32_t>(lexeme_count),
                        static_cast<std::uint32_t>(lexeme_stride),
                        std::move(lexeme_records)});
    sections.push_back({SectionType::stem_references, record_flags,
                        static_cast<std::uint32_t>(stem_reference_count),
                        static_cast<std::uint32_t>(stem_reference_stride),
                        std::move(stem_reference_records)});
    sections.push_back({SectionType::stem_prefix_boundaries,
                        wwdb::section_flag_row_major, wwdb::stem_boundary_count,
                        wwdb::boundary_stride, std::move(stem_boundaries)});
    sections.push_back({SectionType::inflections, record_flags,
                        dense_inflection_count,
                        use_dense_records ? wwdb::inflection_stride
                                          : legacy_simple_inflection_stride,
                        std::move(inflection_records)});
    sections.push_back({SectionType::inflection_section_boundaries,
                        wwdb::section_flag_row_major,
                        wwdb::inflection_boundary_count, wwdb::boundary_stride,
                        std::move(inflection_boundaries)});
    sections.push_back({SectionType::inflection_quantities,
                        wwdb::section_flag_row_major, dense_inflection_count,
                        wwdb::inflection_quantity_stride,
                        std::move(inflection_quantity_records)});
    sections.push_back({
        SectionType::stem_quantities,
        wwdb::section_flag_row_major,
        static_cast<std::uint32_t>(packed_stem_quantities.size()),
        wwdb::stem_quantity_stride,
        std::move(stem_quantity_records),
    });
    sections.push_back({
        SectionType::morphological_notices,
        wwdb::section_flag_row_major,
        static_cast<std::uint32_t>(packed_morphological_notices.size()),
        wwdb::morphological_notice_stride,
        std::move(morphological_notice_records),
    });
    sections.push_back({SectionType::suffix_strings, wwdb::section_flag_pool,
                        suffix_string_pool.size(), wwdb::variable_stride,
                        suffix_string_pool.encode()});
    if (include_meanings) {
        sections.push_back({SectionType::suffix_meanings,
                            wwdb::section_flag_pool, suffix_meaning_pool.size(),
                            wwdb::variable_stride,
                            suffix_meaning_pool.encode()});
    }
    sections.push_back({SectionType::suffixes, wwdb::section_flag_row_major,
                        static_cast<std::uint32_t>(addons.suffixes.size()),
                        suffix_stride, std::move(suffix_records)});
    if (persist_stem_index) {
        sections.push_back(
            {SectionType::addon_attributes, wwdb::section_flag_row_major,
             static_cast<std::uint32_t>(suffix_attributes.size()),
             wwdb::addon_attribute_stride, std::move(addon_attribute_records)});
    }
    sections.push_back({SectionType::prefix_strings, wwdb::section_flag_pool,
                        prefix_string_pool.size(), wwdb::variable_stride,
                        prefix_string_pool.encode()});
    if (include_meanings) {
        sections.push_back({SectionType::prefix_meanings,
                            wwdb::section_flag_pool, prefix_meaning_pool.size(),
                            wwdb::variable_stride,
                            prefix_meaning_pool.encode()});
    }
    sections.push_back({SectionType::prefixes, wwdb::section_flag_row_major,
                        static_cast<std::uint32_t>(addons.prefixes.size()),
                        prefix_stride, std::move(prefix_records)});
    sections.push_back({SectionType::tackon_strings, wwdb::section_flag_pool,
                        tackon_string_pool.size(), wwdb::variable_stride,
                        tackon_string_pool.encode()});
    if (include_meanings) {
        sections.push_back({SectionType::tackon_meanings,
                            wwdb::section_flag_pool, tackon_meaning_pool.size(),
                            wwdb::variable_stride,
                            tackon_meaning_pool.encode()});
    }
    sections.push_back({SectionType::tackons, wwdb::section_flag_row_major,
                        static_cast<std::uint32_t>(addons.tackons.size()),
                        tackon_stride, std::move(tackon_records)});
    sections.push_back({SectionType::uniques, wwdb::section_flag_row_major,
                        static_cast<std::uint32_t>(uniques.size()),
                        unique_stride, std::move(unique_records)});
    sections.push_back({SectionType::rewrite_strings, wwdb::section_flag_pool,
                        rewrite_string_pool.size(), wwdb::variable_stride,
                        rewrite_string_pool.encode()});
    if (include_meanings) {
        sections.push_back({SectionType::rewrite_meanings,
                            wwdb::section_flag_pool,
                            rewrite_meaning_pool.size(), wwdb::variable_stride,
                            rewrite_meaning_pool.encode()});
    }
    sections.push_back({SectionType::rewrites, wwdb::section_flag_row_major,
                        static_cast<std::uint32_t>(rewrites.size()),
                        rewrite_stride, std::move(rewrite_records)});

    const auto minor_version = persist_stem_index
                                   ? wwdb::addon_attributes_minor_version
                                   : wwdb::morphological_notices_minor_version;
    const auto image = make_image(std::move(sections), profile, minor_version);
    std::filesystem::create_directories(output_path.parent_path());
    write_file(output_path, image);

    std::cout << "wrote " << output_path << "\n"
              << "bytes=" << image.size() << "\n"
              << "profile=" << std::to_underlying(profile) << "\n"
              << "minor_version=" << minor_version << "\n"
              << "lexemes=" << lexeme_count << "\n"
              << "stem_strings=" << stem_pool.size() << "\n"
              << "meaning_strings=" << meaning_pool.size() << "\n"
              << "ending_strings=" << ending_pool.size() << "\n"
              << "stem_references=" << stem_reference_count << "\n"
              << "inflections=" << dense_inflection_count << "\n"
              << "inflection_quantities=" << quantities.inflections.size()
              << "\n"
              << "stem_quantities=" << packed_stem_quantities.size() << "\n"
              << "prefixes=" << addons.prefixes.size() << "\n"
              << "suffixes=" << addons.suffixes.size() << "\n"
              << "tackons=" << addons.tackons.size() << "\n"
              << "uniques=" << uniques.size() << "\n"
              << "rewrites=" << rewrites.size() << "\n"
              << "packons="
              << std::ranges::count(addons.tackons, true, &TackonSource::packon)
              << "\n";
    return 0;
} catch (const std::exception &error) {
    std::cerr << "wwdb_poc_pack: " << error.what() << '\n';
    return 1;
}
