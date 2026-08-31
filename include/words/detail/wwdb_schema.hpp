#pragma once

#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <utility>

namespace words::detail::wwdb {

template <std::unsigned_integral Integer>
[[nodiscard]] constexpr Integer low_mask(const std::size_t width) noexcept {
    return width >= std::numeric_limits<Integer>::digits
               ? std::numeric_limits<Integer>::max()
           : width == 0U ? Integer{0}
                         : static_cast<Integer>((std::uintmax_t{1U} << width) -
                                                std::uintmax_t{1U});
}

[[nodiscard]] constexpr std::size_t wire_bytes(const std::size_t bits) {
    return bits / std::numeric_limits<std::uint8_t>::digits;
}

inline constexpr std::size_t bits_per_byte =
    std::numeric_limits<std::uint8_t>::digits;
inline constexpr std::size_t u16_size = wire_bytes(16U);
inline constexpr std::size_t u24_size = wire_bytes(24U);
inline constexpr std::size_t u32_size = wire_bytes(32U);
inline constexpr std::size_t u48_size = wire_bytes(48U);
inline constexpr std::size_t u64_size = wire_bytes(64U);

inline constexpr std::array<std::uint8_t, 8U> magic{'W',  'W',  'D',   'B',
                                                    '\r', '\n', 0x1aU, '\n'};
inline constexpr std::uint16_t major_version = 1U;
inline constexpr std::uint16_t legacy_minor_version = 6U;
inline constexpr std::uint16_t quantity_minor_version = 7U;
inline constexpr std::uint16_t typed_packon_minor_version = 8U;

enum class Profile : std::uint32_t {
    simple = 1U,
    dense = 2U,
    columnar = 3U,
    search_only = 4U,
};

enum class SectionType : std::uint32_t {
    stem_strings = 1U,
    meaning_strings = 2U,
    ending_strings = 3U,
    lexemes = 4U,
    stem_references = 5U,
    stem_prefix_boundaries = 6U,
    inflections = 7U,
    inflection_section_boundaries = 8U,
    suffix_strings = 9U,
    suffix_meanings = 10U,
    suffixes = 11U,
    prefix_strings = 12U,
    prefix_meanings = 13U,
    prefixes = 14U,
    tackon_strings = 15U,
    tackon_meanings = 16U,
    tackons = 17U,
    uniques = 18U,
    rewrite_strings = 19U,
    rewrite_meanings = 20U,
    rewrites = 21U,
    inflection_quantities = 22U,
    stem_quantities = 23U,
};

inline constexpr std::uint32_t minimum_section_type =
    std::to_underlying(SectionType::stem_strings);
inline constexpr std::uint32_t legacy_maximum_section_type =
    std::to_underlying(SectionType::rewrites);
inline constexpr std::uint32_t quantity_maximum_section_type =
    std::to_underlying(SectionType::stem_quantities);

inline constexpr std::size_t header_major_offset = magic.size();
inline constexpr std::size_t header_minor_offset =
    header_major_offset + u16_size;
inline constexpr std::size_t header_size_offset =
    header_minor_offset + u16_size;
inline constexpr std::size_t header_section_count_offset =
    header_size_offset + u32_size;
inline constexpr std::size_t header_profile_offset =
    header_section_count_offset + u32_size;
inline constexpr std::size_t header_file_size_offset =
    header_profile_offset + u32_size;
inline constexpr std::size_t header_crc32_offset =
    header_file_size_offset + u64_size;
inline constexpr std::size_t header_reserved_offset =
    header_crc32_offset + u32_size;
inline constexpr std::size_t fixed_header_size =
    header_reserved_offset + u32_size;

inline constexpr std::size_t directory_type_offset = 0U;
inline constexpr std::size_t directory_flags_offset =
    directory_type_offset + u32_size;
inline constexpr std::size_t directory_payload_offset =
    directory_flags_offset + u32_size;
inline constexpr std::size_t directory_byte_size_offset =
    directory_payload_offset + u64_size;
inline constexpr std::size_t directory_count_offset =
    directory_byte_size_offset + u64_size;
inline constexpr std::size_t directory_stride_offset =
    directory_count_offset + u32_size;
inline constexpr std::size_t directory_entry_size =
    directory_stride_offset + u32_size;

inline constexpr std::uint32_t section_flag_pool = 0U;
inline constexpr std::uint32_t section_flag_row_major = 1U;
inline constexpr std::uint32_t section_flag_columnar = 2U;
inline constexpr std::uint32_t variable_stride = 0U;
inline constexpr std::uint32_t reserved_value = 0U;
inline constexpr std::uint32_t unused_field_value = 0U;
inline constexpr std::uint32_t no_meaning_id = 0U;
inline constexpr std::uint32_t no_quantity_bits = 0U;
inline constexpr std::uint32_t crc32_initial_value = 0U;
inline constexpr std::uint32_t crc32_lsb_mask = 1U;
inline constexpr std::uint32_t crc32_reflected_polynomial = 0xedb8'8320U;

inline constexpr std::size_t lexical_slot_count = 4U;
inline constexpr std::size_t maximum_ending_size = 7U;
inline constexpr std::size_t maximum_stem_size = 18U;
inline constexpr std::size_t lexeme_id_width = 16U;
inline constexpr std::size_t lexical_slot_width = 2U;
inline constexpr std::size_t stem_quantity_slot_shift = lexeme_id_width;
inline constexpr std::size_t stem_quantity_key_width =
    stem_quantity_slot_shift + lexical_slot_width;
inline constexpr std::uint32_t stem_quantity_lexeme_mask =
    low_mask<std::uint32_t>(lexeme_id_width);
inline constexpr std::uint32_t stem_quantity_slot_mask =
    low_mask<std::uint32_t>(lexical_slot_width);
inline constexpr std::uint32_t stem_quantity_value_mask =
    low_mask<std::uint32_t>(maximum_stem_size);
inline constexpr std::uint16_t inflection_quantity_value_mask =
    low_mask<std::uint16_t>(maximum_ending_size);
inline constexpr std::uint16_t inflection_quantity_reserved_mask =
    static_cast<std::uint16_t>(~static_cast<std::uint16_t>(
        inflection_quantity_value_mask |
        (inflection_quantity_value_mask << maximum_ending_size)));
inline constexpr std::uint32_t inflection_quantity_stride = u16_size;
inline constexpr std::uint32_t stem_quantity_stride = u24_size * 3U;
inline constexpr std::uint32_t stem_quantity_known_offset = u24_size;
inline constexpr std::uint32_t stem_quantity_long_vowel_offset =
    stem_quantity_known_offset + u24_size;

inline constexpr std::uint32_t lexeme_stem_id_size = u16_size;
inline constexpr std::uint32_t lexeme_meaning_id_offset =
    lexical_slot_count * lexeme_stem_id_size;
inline constexpr std::uint32_t search_lexeme_metadata_offset =
    lexeme_meaning_id_offset;
inline constexpr std::uint32_t full_lexeme_metadata_offset =
    lexeme_meaning_id_offset + u16_size;
inline constexpr std::uint32_t search_lexeme_stride =
    search_lexeme_metadata_offset + u48_size;
inline constexpr std::uint32_t full_lexeme_stride =
    full_lexeme_metadata_offset + u48_size;

inline constexpr std::uint32_t stem_reference_stride = u24_size;
inline constexpr std::uint32_t boundary_stride = u16_size;
inline constexpr std::uint32_t inflection_stride = u48_size;
inline constexpr std::uint32_t alphabet_size = 26U;
inline constexpr std::uint32_t stem_bucket_count =
    1U + alphabet_size + (alphabet_size * alphabet_size);
inline constexpr std::uint32_t stem_boundary_count = stem_bucket_count + 1U;
inline constexpr std::uint32_t inflection_section_count = 5U;
inline constexpr std::uint32_t inflection_boundary_count =
    inflection_section_count + 1U;

inline constexpr std::uint32_t addon_id_offset = 0U;
inline constexpr std::uint32_t addon_fix_id_offset = addon_id_offset + u16_size;
inline constexpr std::uint32_t addon_meaning_id_offset =
    addon_fix_id_offset + u16_size;
inline constexpr std::uint32_t search_addon_metadata_offset =
    addon_meaning_id_offset;
inline constexpr std::uint32_t full_addon_metadata_offset =
    addon_meaning_id_offset + u16_size;

inline constexpr std::uint32_t search_suffix_stride =
    search_addon_metadata_offset + u64_size;
inline constexpr std::uint32_t full_suffix_stride =
    full_addon_metadata_offset + u64_size;
inline constexpr std::uint32_t search_prefix_stride =
    search_addon_metadata_offset + u16_size;
inline constexpr std::uint32_t full_prefix_stride =
    full_addon_metadata_offset + u16_size;
inline constexpr std::uint32_t search_tackon_stride =
    search_addon_metadata_offset + u32_size;
inline constexpr std::uint32_t full_tackon_stride =
    full_addon_metadata_offset + u32_size;

inline constexpr std::uint32_t unique_surface_id_offset = 0U;
inline constexpr std::uint32_t unique_meaning_id_offset = u16_size;
inline constexpr std::uint32_t search_unique_metadata_offset =
    unique_meaning_id_offset;
inline constexpr std::uint32_t full_unique_metadata_offset =
    unique_meaning_id_offset + u16_size;
inline constexpr std::uint32_t search_unique_stride =
    search_unique_metadata_offset + u64_size;
inline constexpr std::uint32_t full_unique_stride =
    full_unique_metadata_offset + u64_size;

inline constexpr std::uint32_t rewrite_id_offset = 0U;
inline constexpr std::uint32_t rewrite_before_id_offset =
    rewrite_id_offset + u16_size;
inline constexpr std::uint32_t rewrite_after_id_offset =
    rewrite_before_id_offset + u16_size;
inline constexpr std::uint32_t rewrite_name_id_offset =
    rewrite_after_id_offset + u16_size;
inline constexpr std::uint32_t rewrite_meaning_id_offset =
    rewrite_name_id_offset + u16_size;
inline constexpr std::uint32_t search_rewrite_metadata_offset =
    rewrite_meaning_id_offset;
inline constexpr std::uint32_t full_rewrite_metadata_offset =
    rewrite_meaning_id_offset + u16_size;
inline constexpr std::uint32_t rewrite_behavior_relative_offset = u32_size;
inline constexpr std::uint32_t search_rewrite_stride =
    search_rewrite_metadata_offset + u32_size + u16_size;
inline constexpr std::uint32_t full_rewrite_stride =
    full_rewrite_metadata_offset + u32_size + u16_size;

inline constexpr std::size_t pos_width = 4U;
inline constexpr std::uint64_t pos_mask = low_mask<std::uint64_t>(pos_width);
inline constexpr std::size_t paradigm_shift = pos_width;
inline constexpr std::size_t paradigm_width = 8U;
inline constexpr std::uint64_t paradigm_mask =
    low_mask<std::uint64_t>(paradigm_width);
inline constexpr std::size_t translation_shift =
    paradigm_shift + paradigm_width;
inline constexpr std::size_t age_width = 4U;
inline constexpr std::size_t subject_width = 4U;
inline constexpr std::size_t geography_width = 5U;
inline constexpr std::size_t frequency_width = 4U;
inline constexpr std::size_t source_width = 5U;
inline constexpr std::size_t age_shift = 0U;
inline constexpr std::size_t subject_shift = age_shift + age_width;
inline constexpr std::size_t geography_shift = subject_shift + subject_width;
inline constexpr std::size_t frequency_shift =
    geography_shift + geography_width;
inline constexpr std::size_t source_shift = frequency_shift + frequency_width;
inline constexpr std::size_t translation_width = source_shift + source_width;
inline constexpr std::uint64_t translation_mask =
    low_mask<std::uint64_t>(translation_width);
inline constexpr std::size_t lexeme_class_payload_shift =
    translation_shift + translation_width;
inline constexpr std::size_t lexeme_class_payload_width = 13U;
inline constexpr std::uint64_t lexeme_class_payload_mask =
    low_mask<std::uint64_t>(lexeme_class_payload_width);

inline constexpr std::uint32_t age_mask = low_mask<std::uint32_t>(age_width);
inline constexpr std::uint32_t subject_mask =
    low_mask<std::uint32_t>(subject_width);
inline constexpr std::uint32_t geography_mask =
    low_mask<std::uint32_t>(geography_width);
inline constexpr std::uint32_t frequency_mask =
    low_mask<std::uint32_t>(frequency_width);
inline constexpr std::uint32_t source_mask =
    low_mask<std::uint32_t>(source_width);

inline constexpr std::size_t morphology_shift = translation_shift;
inline constexpr std::size_t morphology_width = 16U;
inline constexpr std::uint64_t morphology_mask =
    low_mask<std::uint64_t>(morphology_width);
inline constexpr std::size_t ending_id_shift =
    morphology_shift + morphology_width;
inline constexpr std::size_t ending_id_width = 9U;
inline constexpr std::uint64_t ending_id_mask =
    low_mask<std::uint64_t>(ending_id_width);
inline constexpr std::size_t inflection_stem_key_shift =
    ending_id_shift + ending_id_width;
inline constexpr std::size_t inflection_stem_key_width = 2U;
inline constexpr std::uint64_t inflection_stem_key_mask =
    low_mask<std::uint64_t>(inflection_stem_key_width);
inline constexpr std::uint8_t inflection_stem_key_bias = 1U;
inline constexpr std::size_t inflection_age_shift =
    inflection_stem_key_shift + inflection_stem_key_width;
inline constexpr std::size_t inflection_frequency_shift =
    inflection_age_shift + age_width;
inline constexpr std::size_t inflection_used_bits =
    inflection_frequency_shift + frequency_width;

inline constexpr std::size_t three_bit_width = 3U;
inline constexpr std::size_t two_bit_width = 2U;
inline constexpr std::size_t nominal_number_shift = three_bit_width;
inline constexpr std::size_t nominal_gender_shift =
    nominal_number_shift + two_bit_width;
inline constexpr std::size_t morphology_byte_shift =
    nominal_gender_shift + three_bit_width;
inline constexpr std::size_t verb_number_shift =
    morphology_byte_shift + two_bit_width;
inline constexpr std::size_t participle_voice_shift =
    morphology_byte_shift + three_bit_width;
inline constexpr std::size_t participle_mood_shift =
    participle_voice_shift + two_bit_width;
inline constexpr std::uint16_t two_bit_mask = low_mask<std::uint16_t>(2U);
inline constexpr std::uint16_t three_bit_mask = low_mask<std::uint16_t>(3U);
inline constexpr std::uint16_t nibble_mask = low_mask<std::uint16_t>(4U);
inline constexpr std::uint16_t byte_mask = low_mask<std::uint16_t>(8U);
inline constexpr std::uint16_t single_bit_mask = low_mask<std::uint16_t>(1U);
inline constexpr std::size_t noun_kind_shift = 3U;
inline constexpr std::size_t pronoun_packon_plus_one_shift = 4U;
inline constexpr std::uint16_t no_required_packon = 0U;
inline constexpr std::uint16_t packon_id_bias = 1U;
inline constexpr std::size_t numeral_value_shift = 3U;
inline constexpr std::uint16_t numeral_value_mask =
    low_mask<std::uint16_t>(10U);
inline constexpr std::size_t simple_class_used_bits = 4U;
inline constexpr std::size_t noun_class_used_bits = 7U;
inline constexpr std::size_t nominal_morphology_used_bits = 8U;
inline constexpr std::size_t adjective_morphology_used_bits = 10U;
inline constexpr std::size_t numeral_morphology_used_bits = 11U;
inline constexpr std::size_t adverb_morphology_used_bits = 2U;
inline constexpr std::size_t verb_morphology_used_bits = 12U;
inline constexpr std::size_t supine_morphology_used_bits = 8U;
inline constexpr std::size_t preposition_morphology_used_bits = 3U;

inline constexpr std::size_t stem_reference_slot_shift = 16U;
inline constexpr std::size_t stem_reference_key_shift = 18U;
inline constexpr std::size_t stem_reference_used_bits = 21U;
inline constexpr std::uint32_t stem_reference_lexeme_mask =
    low_mask<std::uint32_t>(16U);
inline constexpr std::uint32_t stem_reference_slot_mask =
    low_mask<std::uint32_t>(2U);
inline constexpr std::uint32_t stem_reference_key_mask =
    low_mask<std::uint32_t>(3U);

inline constexpr std::size_t unique_translation_shift =
    morphology_shift + morphology_width;
inline constexpr std::size_t unique_used_bits =
    unique_translation_shift + translation_width;

inline constexpr std::size_t suffix_root_key_shift = pos_width;
inline constexpr std::size_t suffix_target_pos_shift =
    suffix_root_key_shift + three_bit_width;
inline constexpr std::size_t suffix_target_key_shift =
    suffix_target_pos_shift + pos_width;
inline constexpr std::size_t suffix_paradigm_shift =
    suffix_target_key_shift + three_bit_width;
inline constexpr std::size_t suffix_attribute_shift =
    suffix_paradigm_shift + paradigm_width;
inline constexpr std::size_t suffix_noun_kind_shift =
    suffix_attribute_shift + pos_width;
inline constexpr std::size_t suffix_numeric_value_shift =
    suffix_noun_kind_shift + pos_width;
inline constexpr std::size_t suffix_connector_shift =
    suffix_numeric_value_shift + bits_per_byte;
inline constexpr std::size_t suffix_used_bits =
    suffix_connector_shift + bits_per_byte;

inline constexpr std::size_t prefix_target_shift = pos_width;
inline constexpr std::size_t prefix_connector_shift =
    prefix_target_shift + pos_width;
inline constexpr std::size_t tackon_attribute_shift =
    paradigm_shift + paradigm_width;
inline constexpr std::size_t tackon_noun_kind_shift =
    tackon_attribute_shift + pos_width;
inline constexpr std::size_t tackon_packon_shift =
    tackon_noun_kind_shift + pos_width;
inline constexpr std::size_t tackon_enclitic_shift = tackon_packon_shift + 1U;
inline constexpr std::size_t tackon_used_bits = tackon_enclitic_shift + 1U;

inline constexpr std::size_t rewrite_scope_shift = two_bit_width;
inline constexpr std::size_t rewrite_priority_shift =
    rewrite_scope_shift + two_bit_width;
inline constexpr std::size_t rewrite_scan_reverse_shift =
    rewrite_priority_shift + bits_per_byte;
inline constexpr std::size_t rewrite_required_pos_shift =
    rewrite_scan_reverse_shift + 1U;
inline constexpr std::size_t rewrite_stem_key_shift =
    rewrite_required_pos_shift + pos_width;
inline constexpr std::size_t rewrite_minimum_before_shift =
    rewrite_stem_key_shift + three_bit_width;
inline constexpr std::size_t rewrite_minimum_after_shift =
    rewrite_minimum_before_shift + pos_width;
inline constexpr std::size_t rewrite_medieval_shift =
    rewrite_minimum_after_shift + pos_width;
inline constexpr std::size_t rewrite_metadata_used_bits =
    rewrite_medieval_shift + 1U;
inline constexpr std::size_t rewrite_stage_shift = three_bit_width;
inline constexpr std::size_t rewrite_constraint_shift =
    rewrite_stage_shift + two_bit_width;
inline constexpr std::size_t rewrite_behavior_used_bits =
    rewrite_constraint_shift + two_bit_width;

inline constexpr std::uint8_t maximum_paradigm_digit = 9U;
inline constexpr std::uint8_t maximum_age_code = 8U;
inline constexpr std::uint8_t maximum_subject_code = 11U;
inline constexpr std::uint8_t maximum_geography_code = 17U;
inline constexpr std::uint8_t maximum_frequency_code = 9U;
inline constexpr std::uint8_t maximum_source_code = 25U;
inline constexpr std::uint8_t maximum_noun_kind_code = 9U;
inline constexpr std::uint8_t maximum_stem_key = 4U;
inline constexpr std::uint8_t maximum_person_code = 3U;
inline constexpr std::uint16_t maximum_numeral_value = 1000U;
inline constexpr std::uint8_t ascii_max_code_point = low_mask<std::uint8_t>(7U);

static_assert(bits_per_byte == 8U);
static_assert(fixed_header_size == 40U);
static_assert(directory_entry_size == 32U);
static_assert(stem_boundary_count == 704U);
static_assert(inflection_boundary_count == 6U);
static_assert(search_lexeme_stride == 14U && full_lexeme_stride == 16U);
static_assert(search_suffix_stride == 12U && full_suffix_stride == 14U);
static_assert(search_prefix_stride == 6U && full_prefix_stride == 8U);
static_assert(search_tackon_stride == 8U && full_tackon_stride == 10U);
static_assert(search_unique_stride == 10U && full_unique_stride == 12U);
static_assert(search_rewrite_stride == 14U && full_rewrite_stride == 16U);
static_assert(inflection_quantity_reserved_mask == 0xc000U);
static_assert(translation_width == 22U);
static_assert(lexeme_class_payload_shift + lexeme_class_payload_width == 47U);
static_assert(inflection_used_bits == 47U);
static_assert(unique_used_bits == 50U);
static_assert(suffix_used_bits == 46U);
static_assert(tackon_used_bits == 22U);
static_assert(rewrite_metadata_used_bits == 29U);
static_assert(rewrite_behavior_used_bits == 7U);
static_assert(stem_reference_used_bits <= u24_size * bits_per_byte);
static_assert(maximum_stem_size <= u24_size * bits_per_byte);
static_assert(lexical_slot_count == (std::size_t{1U} << lexical_slot_width));

} // namespace words::detail::wwdb
