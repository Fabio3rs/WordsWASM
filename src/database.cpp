#include "words/database.hpp"
#include "words/lifetime.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <ranges>
#include <stdexcept>
#include <string>
#include <tuple>
#include <utility>

namespace words {
namespace {

constexpr std::size_t fixed_header_size = 40;
constexpr std::size_t directory_entry_size = 32;
constexpr std::uint16_t legacy_wwdb_minor = 6;
constexpr std::uint16_t quantity_wwdb_minor = 7;
constexpr std::uint16_t typed_packon_wwdb_minor = 8;
constexpr std::uint32_t legacy_maximum_section_type = 21;
constexpr std::uint32_t quantity_maximum_section_type = 23;
constexpr std::uint32_t inflection_quantity_stride = 2;
constexpr std::uint32_t stem_quantity_stride = 9;
constexpr std::size_t maximum_ending_size = 7;
constexpr std::size_t maximum_stem_size = 18;
constexpr std::uint16_t inflection_quantity_value_mask = 0x007fU;
constexpr std::uint16_t inflection_quantity_reserved_mask = 0xc000U;
constexpr std::uint32_t stem_quantity_lexeme_mask = 0xffffU;
constexpr std::uint32_t stem_quantity_slot_mask = 0x03U;
constexpr std::uint32_t stem_quantity_slot_shift = 16U;
constexpr std::uint32_t stem_quantity_key_width = 18U;
constexpr std::uint8_t stem_quantity_slot_count = 4U;
constexpr std::uint32_t dense_profile = 2U;
constexpr std::uint32_t search_profile = 4U;

enum class SectionType : std::uint32_t {
    stem_strings = 1,
    meaning_strings = 2,
    ending_strings = 3,
    lexemes = 4,
    stem_references = 5,
    stem_prefix_boundaries = 6,
    inflections = 7,
    inflection_section_boundaries = 8,
    suffix_strings = 9,
    suffix_meanings = 10,
    suffixes = 11,
    prefix_strings = 12,
    prefix_meanings = 13,
    prefixes = 14,
    tackon_strings = 15,
    tackon_meanings = 16,
    tackons = 17,
    uniques = 18,
    rewrite_strings = 19,
    rewrite_meanings = 20,
    rewrites = 21,
    inflection_quantities = 22,
    stem_quantities = 23,
};

struct SectionView final {
    SectionType type{};
    std::uint32_t flags{};
    std::uint64_t offset{};
    std::uint64_t bytes{};
    std::uint32_t count{};
    std::uint32_t stride{};
};

class LoadFailure final : public std::runtime_error {
  public:
    LoadFailure(std::string code, std::string message)
        : std::runtime_error{message}, code_{std::move(code)} {}

    [[nodiscard]] const std::string &code() const noexcept WORDS_LIFETIMEBOUND {
        return code_;
    }

  private:
    std::string code_;
};

[[noreturn]] void fail(std::string code, std::string message) {
    throw LoadFailure{std::move(code), std::move(message)};
}

[[nodiscard]] std::uint8_t byte_at(const std::span<const std::byte> bytes,
                                   const std::size_t offset) {
    if (offset >= bytes.size()) {
        fail("truncated-database", "read extends beyond WWDB image");
    }
    return std::to_integer<std::uint8_t>(bytes[offset]);
}

[[nodiscard]] std::uint16_t read_u16_le(const std::span<const std::byte> bytes,
                                        const std::size_t offset) {
    if (offset > bytes.size() || bytes.size() - offset < 2U) {
        fail("truncated-database", "u16 extends beyond WWDB image");
    }
    return static_cast<std::uint16_t>(byte_at(bytes, offset)) |
           static_cast<std::uint16_t>(
               static_cast<std::uint16_t>(byte_at(bytes, offset + 1U)) << 8U);
}

[[nodiscard]] std::uint32_t read_u24_le(const std::span<const std::byte> bytes,
                                        const std::size_t offset) {
    if (offset > bytes.size() || bytes.size() - offset < 3U) {
        fail("truncated-database", "u24 extends beyond WWDB image");
    }
    return static_cast<std::uint32_t>(byte_at(bytes, offset)) |
           (static_cast<std::uint32_t>(byte_at(bytes, offset + 1U)) << 8U) |
           (static_cast<std::uint32_t>(byte_at(bytes, offset + 2U)) << 16U);
}

[[nodiscard]] std::uint32_t read_u32_le(const std::span<const std::byte> bytes,
                                        const std::size_t offset) {
    if (offset > bytes.size() || bytes.size() - offset < 4U) {
        fail("truncated-database", "u32 extends beyond WWDB image");
    }
    return static_cast<std::uint32_t>(byte_at(bytes, offset)) |
           (static_cast<std::uint32_t>(byte_at(bytes, offset + 1U)) << 8U) |
           (static_cast<std::uint32_t>(byte_at(bytes, offset + 2U)) << 16U) |
           (static_cast<std::uint32_t>(byte_at(bytes, offset + 3U)) << 24U);
}

[[nodiscard]] std::uint64_t read_u64_le(const std::span<const std::byte> bytes,
                                        const std::size_t offset) {
    const auto low = read_u32_le(bytes, offset);
    const auto high = read_u32_le(bytes, offset + 4U);
    return static_cast<std::uint64_t>(low) |
           (static_cast<std::uint64_t>(high) << 32U);
}

[[nodiscard]] std::uint32_t crc32(const std::span<const std::byte> bytes,
                                  std::uint32_t crc = 0) noexcept {
    crc = ~crc;
    for (const auto item : bytes) {
        crc ^= std::to_integer<std::uint8_t>(item);
        for (int bit = 0; bit < 8; ++bit) {
            const auto mask = static_cast<std::uint32_t>(
                -static_cast<std::int32_t>(crc & 1U));
            crc = (crc >> 1U) ^ (0xedb8'8320U & mask);
        }
    }
    return ~crc;
}

[[nodiscard]] char normalized_char(char value) noexcept {
    if (value >= 'A' && value <= 'Z') {
        value = static_cast<char>(value - 'A' + 'a');
    }
    if (value == 'j') {
        return 'i';
    }
    if (value == 'v') {
        return 'u';
    }
    return value;
}

[[nodiscard]] int normalized_compare(const std::string_view left,
                                     const std::string_view right) noexcept {
    const auto common = std::min(left.size(), right.size());
    for (std::size_t index = 0; index < common; ++index) {
        const auto left_char = normalized_char(left[index]);
        const auto right_char = normalized_char(right[index]);
        if (left_char != right_char) {
            return left_char < right_char ? -1 : 1;
        }
    }
    if (left.size() == right.size()) {
        return 0;
    }
    return left.size() < right.size() ? -1 : 1;
}

[[nodiscard]] bool normalized_less(const std::string_view left,
                                   const std::string_view right) noexcept {
    return normalized_compare(left, right) < 0;
}

[[nodiscard]] bool normalized_equal(const std::string_view left,
                                    const std::string_view right) noexcept {
    return normalized_compare(left, right) == 0;
}

[[nodiscard]] std::span<const std::byte>
section_bytes(const std::span<const std::byte> image,
              const SectionView &section) {
    const auto offset = static_cast<std::size_t>(section.offset);
    const auto count = static_cast<std::size_t>(section.bytes);
    return image.subspan(offset, count);
}

class RecordView final {
  public:
    RecordView(const std::span<const std::byte> bytes WORDS_LIFETIMEBOUND,
               const SectionView &section)
        : bytes_{bytes}, count_{section.count}, stride_{section.stride},
          columnar_{section.flags == 2U} {}

    [[nodiscard]] std::uint8_t byte(const std::uint32_t record,
                                    const std::uint32_t field) const {
        if (record >= count_ || field >= stride_) {
            fail("truncated-database", "record field is outside its section");
        }
        const auto offset = columnar_
                                ? static_cast<std::size_t>(field) * count_ +
                                      record
                                : static_cast<std::size_t>(record) * stride_ +
                                      field;
        return byte_at(bytes_, offset);
    }

    [[nodiscard]] std::uint16_t read_u16(const std::uint32_t record,
                                         const std::uint32_t field) const {
        return static_cast<std::uint16_t>(byte(record, field)) |
               static_cast<std::uint16_t>(
                   static_cast<std::uint16_t>(byte(record, field + 1U)) << 8U);
    }

    [[nodiscard]] std::uint32_t read_u24(const std::uint32_t record,
                                         const std::uint32_t field) const {
        return static_cast<std::uint32_t>(byte(record, field)) |
               (static_cast<std::uint32_t>(byte(record, field + 1U)) << 8U) |
               (static_cast<std::uint32_t>(byte(record, field + 2U)) << 16U);
    }

    [[nodiscard]] std::uint32_t read_u32(const std::uint32_t record,
                                         const std::uint32_t field) const {
        return read_u24(record, field) |
               (static_cast<std::uint32_t>(byte(record, field + 3U)) << 24U);
    }

    [[nodiscard]] std::uint64_t read_u48(const std::uint32_t record,
                                         const std::uint32_t field) const {
        return static_cast<std::uint64_t>(read_u32(record, field)) |
               (static_cast<std::uint64_t>(read_u16(record, field + 4U))
                << 32U);
    }

    [[nodiscard]] std::uint64_t read_u64(const std::uint32_t record,
                                         const std::uint32_t field) const {
        return static_cast<std::uint64_t>(read_u32(record, field)) |
               (static_cast<std::uint64_t>(read_u32(record, field + 4U))
                << 32U);
    }

  private:
    std::span<const std::byte> bytes_;
    std::uint32_t count_{};
    std::uint32_t stride_{};
    bool columnar_{};
};

[[nodiscard]] const SectionView &
find_section(const std::vector<SectionView> &sections, const SectionType type) {
    const auto found = std::ranges::find(sections, type, &SectionView::type);
    if (found == sections.end()) {
        fail("missing-section", "required WWDB section is absent");
    }
    return *found;
}

[[nodiscard]] const SectionView *
find_optional_section(const std::vector<SectionView> &sections,
                      const SectionType type) noexcept {
    const auto found = std::ranges::find(sections, type, &SectionView::type);
    return found == sections.end() ? nullptr : &*found;
}

void require_shape(const SectionView &section, const std::uint32_t flags,
                   const std::uint32_t stride) {
    if (section.flags != flags || section.stride != stride) {
        fail("unsupported-section-layout",
             "WWDB section flags or stride are unsupported");
    }
    if (stride != 0U &&
        section.bytes != static_cast<std::uint64_t>(section.count) * stride) {
        fail("invalid-section-size",
             "WWDB fixed-record section has inconsistent size");
    }
}

void parse_string_pool(const std::span<const std::byte> bytes,
                       const std::uint32_t expected_count,
                       std::vector<std::string_view> &output) {
    output.clear();
    output.reserve(expected_count);
    std::size_t offset = 0;
    while (offset < bytes.size()) {
        const auto length = static_cast<std::size_t>(byte_at(bytes, offset));
        ++offset;
        if (length > bytes.size() - offset) {
            fail("invalid-string-pool", "string extends beyond its WWDB pool");
        }
        const auto string_bytes = bytes.subspan(offset, length);
        output.emplace_back(reinterpret_cast<const char *>(string_bytes.data()),
                            string_bytes.size());
        offset += length;
    }
    if (output.size() != static_cast<std::size_t>(expected_count)) {
        fail("invalid-string-pool", "string count differs from WWDB directory");
    }
}

[[nodiscard]] std::uint8_t checked_nibble(const std::uint8_t value,
                                          const char *field) {
    if (value > 9U) {
        fail("invalid-enum", std::string{field} + " is outside 0..9");
    }
    return value;
}

[[nodiscard]] constexpr std::uint32_t
low_bits(const std::size_t width) noexcept {
    if (width >= std::numeric_limits<std::uint32_t>::digits) {
        return std::numeric_limits<std::uint32_t>::max();
    }
    return width == 0U ? 0U : (std::uint32_t{1U} << width) - 1U;
}

[[nodiscard]] bool
quantity_positions_are_vowels(const std::string_view text,
                              const std::uint32_t known) noexcept {
    for (std::size_t index = 0; index < text.size(); ++index) {
        if ((known & (std::uint32_t{1U} << index)) == 0U) {
            continue;
        }
        const auto letter = normalized_char(text[index]);
        if (letter != 'a' && letter != 'e' && letter != 'i' && letter != 'o' &&
            letter != 'u' && letter != 'y') {
            return false;
        }
    }
    return true;
}

struct ParsedStemQuantity final {
    std::uint32_t key{};
    QuantityMask quantity;
};

[[nodiscard]] std::vector<QuantityMask>
parse_inflection_quantities(const std::span<const std::byte> image,
                            const SectionView &section,
                            const std::span<const InflectionRule> rules,
                            const std::span<const std::string_view> endings) {
    if (section.count != rules.size()) {
        fail("invalid-section-size",
             "inflection quantity count differs from rule count");
    }
    const auto bytes = section_bytes(image, section);
    std::vector<QuantityMask> output;
    output.reserve(section.count);
    for (std::uint32_t ordinal = 0; ordinal < section.count; ++ordinal) {
        const auto packed =
            read_u16_le(bytes, static_cast<std::size_t>(ordinal) *
                                   inflection_quantity_stride);
        if ((packed & inflection_quantity_reserved_mask) != 0U) {
            fail("reserved-bits",
                 "inflection quantity has nonzero reserved bits");
        }
        const QuantityMask quantity{
            static_cast<std::uint32_t>(packed & inflection_quantity_value_mask),
            static_cast<std::uint32_t>((packed >> maximum_ending_size) &
                                       inflection_quantity_value_mask),
        };
        const auto ending = endings[rules[ordinal].ending.value()];
        const auto valid_bits = low_bits(ending.size());
        if (ending.size() > maximum_ending_size ||
            (quantity.long_vowel & ~quantity.known) != 0U ||
            ((quantity.known | quantity.long_vowel) & ~valid_bits) != 0U ||
            !quantity_positions_are_vowels(ending, quantity.known)) {
            fail("invalid-quantity-mask",
                 "inflection quantity is inconsistent with its ending");
        }
        output.push_back(quantity);
    }
    return output;
}

[[nodiscard]] std::vector<ParsedStemQuantity>
parse_stem_quantities(const std::span<const std::byte> image,
                      const SectionView &section,
                      const std::span<const LexemeRecord> lexemes,
                      const std::span<const std::string_view> stems) {
    const auto bytes = section_bytes(image, section);
    std::vector<ParsedStemQuantity> output;
    output.reserve(section.count);
    std::optional<std::uint32_t> previous_key;
    for (std::uint32_t ordinal = 0; ordinal < section.count; ++ordinal) {
        const auto offset =
            static_cast<std::size_t>(ordinal) * stem_quantity_stride;
        const auto key = read_u24_le(bytes, offset);
        const auto lexeme_id = key & stem_quantity_lexeme_mask;
        const auto lexical_slot =
            (key >> stem_quantity_slot_shift) & stem_quantity_slot_mask;
        const QuantityMask quantity{
            read_u24_le(bytes, offset + 3U),
            read_u24_le(bytes, offset + 6U),
        };
        if ((key >> stem_quantity_key_width) != 0U ||
            lexeme_id >= lexemes.size()) {
            fail("invalid-reference",
                 "stem quantity key is outside the lexical table");
        }
        if (previous_key && key <= *previous_key) {
            fail("invalid-order",
                 "stem quantity keys must be unique and increasing");
        }
        previous_key = key;
        const auto &lexeme = lexemes[lexeme_id];
        const auto stem =
            stems[lexeme.stems[static_cast<std::size_t>(lexical_slot)].value()];
        const auto valid_bits = low_bits(stem.size());
        if (stem.empty() || stem.size() > maximum_stem_size ||
            quantity.known == 0U ||
            (quantity.long_vowel & ~quantity.known) != 0U ||
            ((quantity.known | quantity.long_vowel) & ~valid_bits) != 0U ||
            !quantity_positions_are_vowels(stem, quantity.known)) {
            fail("invalid-quantity-mask",
                 "stem quantity is inconsistent with its lexical slot");
        }
        output.push_back({key, quantity});
    }
    return output;
}

void validate_section_shapes(const std::vector<SectionView> &sections,
                             const DatabaseContent content) {
    const auto search = content == DatabaseContent::search;
    const auto record_flags = search ? 2U : 1U;
    const auto require_meaning_pool = [&](const SectionType type) {
        const auto *section = find_optional_section(sections, type);
        if (search) {
            if (section != nullptr) {
                fail("unexpected-section",
                     "search WWDB must not contain a meaning pool");
            }
        } else {
            require_shape(find_section(sections, type), 0U, 0U);
        }
    };

    require_shape(find_section(sections, SectionType::stem_strings), 0U, 0U);
    require_meaning_pool(SectionType::meaning_strings);
    require_shape(find_section(sections, SectionType::ending_strings), 0U, 0U);
    require_shape(find_section(sections, SectionType::lexemes), record_flags,
                  search ? 14U : 16U);
    require_shape(find_section(sections, SectionType::stem_references),
                  record_flags, 3U);
    require_shape(find_section(sections, SectionType::stem_prefix_boundaries),
                  1U, 2U);
    require_shape(find_section(sections, SectionType::inflections),
                  record_flags, 6U);
    require_shape(
        find_section(sections, SectionType::inflection_section_boundaries), 1U,
        2U);
    require_shape(find_section(sections, SectionType::suffix_strings), 0U, 0U);
    require_meaning_pool(SectionType::suffix_meanings);
    require_shape(find_section(sections, SectionType::suffixes), 1U,
                  search ? 12U : 14U);
    require_shape(find_section(sections, SectionType::prefix_strings), 0U, 0U);
    require_meaning_pool(SectionType::prefix_meanings);
    require_shape(find_section(sections, SectionType::prefixes), 1U,
                  search ? 6U : 8U);
    require_shape(find_section(sections, SectionType::tackon_strings), 0U, 0U);
    require_meaning_pool(SectionType::tackon_meanings);
    require_shape(find_section(sections, SectionType::tackons), 1U,
                  search ? 8U : 10U);
    require_shape(find_section(sections, SectionType::uniques), 1U,
                  search ? 10U : 12U);
    require_shape(find_section(sections, SectionType::rewrite_strings), 0U, 0U);
    require_meaning_pool(SectionType::rewrite_meanings);
    require_shape(find_section(sections, SectionType::rewrites), 1U,
                  search ? 14U : 16U);

    if (const auto *section = find_optional_section(
            sections, SectionType::inflection_quantities)) {
        require_shape(*section, 1U, inflection_quantity_stride);
    }
    if (const auto *section =
            find_optional_section(sections, SectionType::stem_quantities)) {
        require_shape(*section, 1U, stem_quantity_stride);
    }

    const auto &stem_boundaries =
        find_section(sections, SectionType::stem_prefix_boundaries);
    const auto &inflection_boundaries =
        find_section(sections, SectionType::inflection_section_boundaries);
    if (stem_boundaries.count != 704U || inflection_boundaries.count != 6U) {
        fail("invalid-boundaries",
             "PoC boundary section has an unexpected count");
    }
}

} // namespace

std::expected<std::unique_ptr<const Database>, LoadError>
Database::load_poc(std::vector<std::byte> image) try {
    if (image.size() < fixed_header_size) {
        fail("truncated-database", "WWDB header is incomplete");
    }
    const auto bytes = std::span<const std::byte>{image};
    constexpr std::array<std::uint8_t, 8> expected_magic{
        'W', 'W', 'D', 'B', '\r', '\n', 0x1a, '\n'};
    for (std::size_t index = 0; index < expected_magic.size(); ++index) {
        if (byte_at(bytes, index) != expected_magic[index]) {
            fail("invalid-magic", "file is not a WWDB image");
        }
    }

    const auto major = read_u16_le(bytes, 8);
    const auto minor = read_u16_le(bytes, 10);
    const auto header_size = read_u32_le(bytes, 12);
    const auto section_count = read_u32_le(bytes, 16);
    const auto profile = read_u32_le(bytes, 20);
    const auto declared_size = read_u64_le(bytes, 24);
    const auto declared_crc = read_u32_le(bytes, 32);
    const auto reserved = read_u32_le(bytes, 36);
    if (major != 1U ||
        (minor != legacy_wwdb_minor && minor != quantity_wwdb_minor &&
         minor != typed_packon_wwdb_minor) ||
        header_size != fixed_header_size) {
        fail("unsupported-version",
             "only PoC WWDB versions 1.6 through 1.8 are supported");
    }
    if (profile != dense_profile && profile != search_profile) {
        fail("unsupported-profile",
             "only the full dense and search-only PoC profiles are supported");
    }
    const auto content = profile == search_profile ? DatabaseContent::search
                                                   : DatabaseContent::full;
    if (content == DatabaseContent::search &&
        minor < typed_packon_wwdb_minor) {
        fail("unsupported-version",
             "search-only WWDB requires typed packon metadata from version 1.8");
    }
    if (reserved != 0U || declared_size != image.size()) {
        fail("invalid-header", "WWDB reserved field or file size is invalid");
    }
    if (section_count == 0U || section_count > 64U) {
        fail("invalid-directory", "WWDB section count is outside safe limits");
    }

    const auto directory_bytes =
        static_cast<std::uint64_t>(section_count) * directory_entry_size;
    const auto payload_offset_u64 =
        static_cast<std::uint64_t>(fixed_header_size) + directory_bytes;
    if (payload_offset_u64 > image.size()) {
        fail("truncated-database", "WWDB directory extends beyond the image");
    }
    const auto payload_offset = static_cast<std::size_t>(payload_offset_u64);

    std::vector<SectionView> sections;
    sections.reserve(section_count);
    for (std::uint32_t index = 0; index < section_count; ++index) {
        const auto offset =
            fixed_header_size +
            static_cast<std::size_t>(index) * directory_entry_size;
        const auto raw_type = read_u32_le(bytes, offset);
        const auto maximum_section_type = minor >= quantity_wwdb_minor
                                              ? quantity_maximum_section_type
                                              : legacy_maximum_section_type;
        if (raw_type < 1U || raw_type > maximum_section_type) {
            fail("unknown-section", "WWDB contains an unknown section type");
        }
        const SectionView section{
            static_cast<SectionType>(raw_type),
            read_u32_le(bytes, offset + 4U),
            read_u64_le(bytes, offset + 8U),
            read_u64_le(bytes, offset + 16U),
            read_u32_le(bytes, offset + 24U),
            read_u32_le(bytes, offset + 28U),
        };
        if (std::ranges::find(sections, section.type, &SectionView::type) !=
            sections.end()) {
            fail("duplicate-section",
                 "WWDB section type occurs more than once");
        }
        if (section.offset > image.size() ||
            section.bytes > image.size() - section.offset) {
            fail("invalid-section-range",
                 "WWDB section extends beyond the image");
        }
        sections.push_back(section);
    }

    auto ordered_sections = sections;
    std::ranges::sort(ordered_sections, {}, &SectionView::offset);
    std::uint64_t expected_offset = payload_offset_u64;
    for (const auto &section : ordered_sections) {
        if (section.offset != expected_offset) {
            fail("invalid-section-range",
                 "WWDB sections overlap or contain gaps");
        }
        expected_offset += section.bytes;
    }
    if (expected_offset != image.size()) {
        fail("invalid-section-range", "WWDB sections do not cover the payload");
    }
    if (crc32(bytes.subspan(payload_offset)) != declared_crc) {
        fail("checksum-mismatch",
             "WWDB payload CRC32 does not match the header");
    }

    const auto &stem_pool_section =
        find_section(sections, SectionType::stem_strings);
    const auto *meaning_pool_section =
        find_optional_section(sections, SectionType::meaning_strings);
    const auto &ending_pool_section =
        find_section(sections, SectionType::ending_strings);
    const auto &lexeme_section = find_section(sections, SectionType::lexemes);
    const auto &stem_reference_section =
        find_section(sections, SectionType::stem_references);
    const auto &stem_boundary_section =
        find_section(sections, SectionType::stem_prefix_boundaries);
    const auto &inflection_section =
        find_section(sections, SectionType::inflections);
    const auto &inflection_boundary_section =
        find_section(sections, SectionType::inflection_section_boundaries);
    const auto &suffix_string_section =
        find_section(sections, SectionType::suffix_strings);
    const auto *suffix_meaning_section =
        find_optional_section(sections, SectionType::suffix_meanings);
    const auto &suffix_section = find_section(sections, SectionType::suffixes);
    const auto &prefix_string_section =
        find_section(sections, SectionType::prefix_strings);
    const auto *prefix_meaning_section =
        find_optional_section(sections, SectionType::prefix_meanings);
    const auto &prefix_section = find_section(sections, SectionType::prefixes);
    const auto &tackon_string_section =
        find_section(sections, SectionType::tackon_strings);
    const auto *tackon_meaning_section =
        find_optional_section(sections, SectionType::tackon_meanings);
    const auto &tackon_section = find_section(sections, SectionType::tackons);
    const auto &unique_section = find_section(sections, SectionType::uniques);
    const auto &rewrite_string_section =
        find_section(sections, SectionType::rewrite_strings);
    const auto *rewrite_meaning_section =
        find_optional_section(sections, SectionType::rewrite_meanings);
    const auto &rewrite_section = find_section(sections, SectionType::rewrites);
    const auto *inflection_quantity_section =
        find_optional_section(sections, SectionType::inflection_quantities);
    const auto *stem_quantity_section =
        find_optional_section(sections, SectionType::stem_quantities);
    if (minor >= quantity_wwdb_minor &&
        (inflection_quantity_section == nullptr ||
         stem_quantity_section == nullptr)) {
        fail("missing-section", "WWDB 1.7 requires both quantity sections");
    }

    // WHY: layout validation belongs to the versioned wire schema and must
    // finish before any record is decoded into runtime objects.
    validate_section_shapes(sections, content);

    auto database = std::unique_ptr<Database>{
        new Database{std::move(image), content}};
    const auto owned_bytes = std::span<const std::byte>{database->image_};
    parse_string_pool(section_bytes(owned_bytes, stem_pool_section),
                      stem_pool_section.count, database->stem_strings_);
    if (meaning_pool_section != nullptr) {
        parse_string_pool(section_bytes(owned_bytes, *meaning_pool_section),
                          meaning_pool_section->count,
                          database->meaning_strings_);
    }
    parse_string_pool(section_bytes(owned_bytes, ending_pool_section),
                      ending_pool_section.count, database->ending_strings_);
    parse_string_pool(section_bytes(owned_bytes, suffix_string_section),
                      suffix_string_section.count, database->suffix_strings_);
    if (suffix_meaning_section != nullptr) {
        parse_string_pool(section_bytes(owned_bytes, *suffix_meaning_section),
                          suffix_meaning_section->count,
                          database->suffix_meanings_);
    }
    parse_string_pool(section_bytes(owned_bytes, prefix_string_section),
                      prefix_string_section.count, database->prefix_strings_);
    if (prefix_meaning_section != nullptr) {
        parse_string_pool(section_bytes(owned_bytes, *prefix_meaning_section),
                          prefix_meaning_section->count,
                          database->prefix_meanings_);
    }
    parse_string_pool(section_bytes(owned_bytes, tackon_string_section),
                      tackon_string_section.count, database->tackon_strings_);
    if (tackon_meaning_section != nullptr) {
        parse_string_pool(section_bytes(owned_bytes, *tackon_meaning_section),
                          tackon_meaning_section->count,
                          database->tackon_meanings_);
    }
    parse_string_pool(section_bytes(owned_bytes, rewrite_string_section),
                      rewrite_string_section.count, database->rewrite_strings_);
    if (rewrite_meaning_section != nullptr) {
        parse_string_pool(section_bytes(owned_bytes, *rewrite_meaning_section),
                          rewrite_meaning_section->count,
                          database->rewrite_meanings_);
    }

    const RecordView rewrite_records{
        section_bytes(owned_bytes, rewrite_section), rewrite_section};
    database->rewrites_.reserve(rewrite_section.count);
    for (std::uint32_t ordinal = 0; ordinal < rewrite_section.count;
         ++ordinal) {
        const auto id = rewrite_records.read_u16(ordinal, 0U);
        const auto before_id = rewrite_records.read_u16(ordinal, 2U);
        const auto after_id = rewrite_records.read_u16(ordinal, 4U);
        const auto name_id = rewrite_records.read_u16(ordinal, 6U);
        const auto meaning_id = content == DatabaseContent::full
                                    ? rewrite_records.read_u16(ordinal, 8U)
                                    : 0U;
        const auto metadata_offset =
            content == DatabaseContent::full ? 10U : 8U;
        const auto metadata =
            rewrite_records.read_u32(ordinal, metadata_offset);
        const auto behavior =
            rewrite_records.read_u16(ordinal, metadata_offset + 4U);
        if (id != ordinal || before_id >= database->rewrite_strings_.size() ||
            after_id >= database->rewrite_strings_.size() ||
            name_id >= database->rewrite_strings_.size() ||
            (content == DatabaseContent::full &&
             meaning_id >= database->rewrite_meanings_.size())) {
            fail("invalid-reference",
                 "rewrite ID or string reference is out of range");
        }
        if ((metadata >> 29U) != 0U) {
            fail("reserved-bits", "rewrite record has nonzero reserved bits");
        }
        if ((behavior >> 7U) != 0U) {
            fail("reserved-bits", "rewrite behavior has nonzero reserved bits");
        }

        RewriteRule rewrite;
        rewrite.id = RewriteId{id};
        rewrite.before = RewriteStringId{before_id};
        rewrite.after = RewriteStringId{after_id};
        rewrite.name = RewriteStringId{name_id};
        rewrite.meaning = RewriteMeaningId{meaning_id};
        rewrite.kind = static_cast<RewriteKind>(metadata & 0x03U);
        rewrite.scope = static_cast<RewriteScope>((metadata >> 2U) & 0x03U);
        rewrite.priority = static_cast<std::uint8_t>((metadata >> 4U) & 0xffU);
        rewrite.scan_reverse = ((metadata >> 12U) & 1U) != 0U;
        rewrite.required_part =
            static_cast<PartOfSpeech>((metadata >> 13U) & 0x0fU);
        rewrite.required_stem_key =
            static_cast<std::uint8_t>((metadata >> 17U) & 0x07U);
        rewrite.minimum_before =
            static_cast<std::uint8_t>((metadata >> 20U) & 0x0fU);
        rewrite.minimum_after =
            static_cast<std::uint8_t>((metadata >> 24U) & 0x0fU);
        rewrite.medieval = ((metadata >> 28U) & 1U) != 0U;
        rewrite.operation = static_cast<RewriteOperation>(behavior & 0x07U);
        rewrite.stage = static_cast<RewriteStage>((behavior >> 3U) & 0x03U);
        rewrite.constraint =
            static_cast<RewriteConstraint>((behavior >> 5U) & 0x03U);
        if (std::to_underlying(rewrite.kind) <
                std::to_underlying(RewriteKind::syncope) ||
            std::to_underlying(rewrite.kind) >
                std::to_underlying(RewriteKind::orthographic) ||
            std::to_underlying(rewrite.scope) <
                std::to_underlying(RewriteScope::initial) ||
            std::to_underlying(rewrite.scope) >
                std::to_underlying(RewriteScope::final) ||
            std::to_underlying(rewrite.required_part) >
                std::to_underlying(PartOfSpeech::verb) ||
            std::to_underlying(rewrite.operation) <
                std::to_underlying(RewriteOperation::literal) ||
            std::to_underlying(rewrite.operation) >
                std::to_underlying(RewriteOperation::double_consonant) ||
            std::to_underlying(rewrite.stage) <
                std::to_underlying(RewriteStage::main) ||
            std::to_underlying(rewrite.stage) >
                std::to_underlying(RewriteStage::fallback) ||
            std::to_underlying(rewrite.constraint) >
                std::to_underlying(RewriteConstraint::adjective_iis) ||
            rewrite.required_stem_key > 4U ||
            (database->rewrite_string(rewrite.before).empty() &&
             rewrite.operation != RewriteOperation::double_consonant) ||
            database->rewrite_string(rewrite.name).empty()) {
            fail("invalid-enum", "rewrite metadata is invalid");
        }
        database->rewrites_.push_back(rewrite);
    }

    const RecordView lexeme_records{section_bytes(owned_bytes, lexeme_section),
                                    lexeme_section};
    database->lexemes_.reserve(static_cast<std::size_t>(lexeme_section.count) +
                               unique_section.count);
    for (std::uint32_t ordinal = 0; ordinal < lexeme_section.count; ++ordinal) {
        LexemeRecord record;
        record.dictionary = DictionaryKind::general;
        record.dictionary_entry = ordinal;
        for (std::size_t slot = 0; slot < record.stems.size(); ++slot) {
            const auto id = lexeme_records.read_u16(
                ordinal, static_cast<std::uint32_t>(slot * 2U));
            if (id >= database->stem_strings_.size()) {
                fail("invalid-reference",
                     "lexeme stem string ID is out of range");
            }
            record.stems[slot] = StringId{id};
        }
        const auto meaning_id = content == DatabaseContent::full
                                    ? lexeme_records.read_u16(ordinal, 8U)
                                    : 0U;
        if (content == DatabaseContent::full &&
            meaning_id >= database->meaning_strings_.size()) {
            fail("invalid-reference",
                 "lexeme meaning string ID is out of range");
        }
        record.meaning = StringId{meaning_id};

        const auto metadata = lexeme_records.read_u48(
            ordinal, content == DatabaseContent::full ? 10U : 8U);
        const auto pofs = static_cast<std::uint8_t>(metadata & 0x0fU);
        const auto paradigm =
            static_cast<std::uint8_t>((metadata >> 4U) & 0xffU);
        const auto translation =
            static_cast<std::uint32_t>((metadata >> 12U) & 0x3f'ffffU);
        const auto class_payload =
            static_cast<std::uint16_t>((metadata >> 34U) & 0x1fffU);
        record.part_of_speech = static_cast<PartOfSpeech>(pofs);
        record.declension = checked_nibble(
            static_cast<std::uint8_t>(paradigm >> 4U), "declension");
        record.variant = checked_nibble(
            static_cast<std::uint8_t>(paradigm & 0x0fU), "variant");
        record.age = static_cast<std::uint8_t>(translation & 0x0fU);
        record.subject = static_cast<std::uint8_t>((translation >> 4U) & 0x0fU);
        record.geography =
            static_cast<std::uint8_t>((translation >> 8U) & 0x1fU);
        record.frequency =
            static_cast<std::uint8_t>((translation >> 13U) & 0x0fU);
        record.source = static_cast<std::uint8_t>((translation >> 17U) & 0x1fU);
        if (record.age > 8U || record.subject > 11U || record.geography > 17U ||
            record.frequency > 9U || record.source > 25U) {
            fail("invalid-enum", "lexical metadata contains an invalid enum");
        }
        if (pofs == std::to_underlying(PartOfSpeech::noun)) {
            record.gender = static_cast<Gender>(class_payload & 0x07U);
            record.noun_kind =
                static_cast<std::uint8_t>((class_payload >> 3U) & 0x0fU);
            if ((class_payload >> 7U) != 0U ||
                std::to_underlying(record.gender) > 4U ||
                record.noun_kind > 9U) {
                fail("invalid-enum",
                     "noun lexical payload contains an invalid enum");
            }
        } else if (pofs == std::to_underlying(PartOfSpeech::pronoun) ||
                   pofs == std::to_underlying(PartOfSpeech::pack)) {
            record.pronoun_kind =
                static_cast<PronounKind>(class_payload & 0x0fU);
            const auto packon_plus_one =
                static_cast<std::uint16_t>(class_payload >> 4U);
            if (pofs == std::to_underlying(PartOfSpeech::pack) &&
                minor >= typed_packon_wwdb_minor && packon_plus_one != 0U) {
                record.required_packon = AddonId{packon_plus_one - 1U};
            }
            if ((pofs == std::to_underlying(PartOfSpeech::pronoun) &&
                 packon_plus_one != 0U) ||
                (minor < typed_packon_wwdb_minor && packon_plus_one != 0U) ||
                std::to_underlying(record.pronoun_kind) > 7U) {
                fail("invalid-enum",
                     "pronoun lexical payload contains an invalid kind");
            }
        } else if (pofs == std::to_underlying(PartOfSpeech::adjective)) {
            record.adjective_degree =
                static_cast<Degree>(class_payload & 0x0fU);
            if ((class_payload >> 4U) != 0U ||
                std::to_underlying(record.adjective_degree) > 3U) {
                fail("invalid-enum",
                     "adjective lexical payload contains an invalid degree");
            }
        } else if (pofs == std::to_underlying(PartOfSpeech::numeral)) {
            record.numeral_type =
                static_cast<NumeralType>(class_payload & 0x07U);
            record.numeral_value =
                static_cast<std::uint16_t>((class_payload >> 3U) & 0x03ffU);
            if (std::to_underlying(record.numeral_type) > 4U ||
                record.numeral_value > 1000U) {
                fail("invalid-enum",
                     "numeral lexical payload contains an invalid value");
            }
        } else if (pofs == std::to_underlying(PartOfSpeech::adverb)) {
            record.adverb_degree = static_cast<Degree>(class_payload & 0x0fU);
            if ((class_payload >> 4U) != 0U ||
                std::to_underlying(record.adverb_degree) > 3U) {
                fail("invalid-enum",
                     "adverb lexical payload contains an invalid degree");
            }
        } else if (pofs == std::to_underlying(PartOfSpeech::verb)) {
            record.verb_kind = static_cast<VerbKind>(class_payload & 0x0fU);
            if ((class_payload >> 4U) != 0U ||
                std::to_underlying(record.verb_kind) > 11U) {
                fail("invalid-enum",
                     "verb lexical payload contains an invalid kind");
            }
        } else if (pofs == std::to_underlying(PartOfSpeech::preposition)) {
            record.governs =
                static_cast<GrammaticalCase>(class_payload & 0x0fU);
            if ((class_payload >> 4U) != 0U ||
                std::to_underlying(record.governs) > 7U) {
                fail("invalid-enum",
                     "preposition lexical payload contains an invalid case");
            }
        } else if (class_payload != 0U) {
            fail("reserved-bits", "unused lexical payload is nonzero");
        }
        database->lexemes_.push_back(record);
    }

    struct IndexedStem final {
        std::string_view key;
        StemReference reference;
    };
    std::vector<IndexedStem> indexed_stems;
    indexed_stems.reserve(stem_reference_section.count);
    const RecordView stem_reference_records{
        section_bytes(owned_bytes, stem_reference_section),
        stem_reference_section};
    for (std::uint32_t ordinal = 0; ordinal < stem_reference_section.count;
         ++ordinal) {
        const auto packed = stem_reference_records.read_u24(ordinal, 0U);
        if ((packed >> 21U) != 0U) {
            fail("reserved-bits", "stem reference has nonzero reserved bits");
        }
        const auto lexeme_id = static_cast<std::uint16_t>(packed & 0xffffU);
        const auto lexical_slot =
            static_cast<std::uint8_t>((packed >> 16U) & 0x03U);
        const auto stem_key =
            static_cast<std::uint8_t>((packed >> 18U) & 0x07U);
        if (lexeme_id >= database->lexemes_.size() || stem_key > 4U) {
            fail("invalid-reference",
                 "stem reference points outside lexical data");
        }
        const StemReference reference{LexemeId{lexeme_id}, lexical_slot,
                                      stem_key};
        const auto stem_id = database->lexemes_[lexeme_id].stems[lexical_slot];
        indexed_stems.push_back({database->stem_string(stem_id), reference});
    }
    std::ranges::sort(indexed_stems,
                      [](const IndexedStem &left, const IndexedStem &right) {
                          const auto key_order =
                              normalized_compare(left.key, right.key);
                          if (key_order != 0) {
                              return key_order < 0;
                          }
                          return std::tuple{left.reference.lexeme.value(),
                                            left.reference.lexical_slot,
                                            left.reference.stem_key} <
                                 std::tuple{right.reference.lexeme.value(),
                                            right.reference.lexical_slot,
                                            right.reference.stem_key};
                      });
    database->stem_references_.reserve(indexed_stems.size());
    database->stem_groups_.reserve(stem_pool_section.count);
    for (std::size_t index = 0; index < indexed_stems.size();) {
        const auto first = database->stem_references_.size();
        const auto key = indexed_stems[index].key;
        do {
            database->stem_references_.push_back(
                indexed_stems[index].reference);
            ++index;
        } while (index < indexed_stems.size() &&
                 normalized_equal(key, indexed_stems[index].key));
        database->stem_groups_.push_back({
            key,
            static_cast<std::uint32_t>(first),
            static_cast<std::uint32_t>(database->stem_references_.size() -
                                       first),
        });
    }

    const RecordView inflection_records{
        section_bytes(owned_bytes, inflection_section), inflection_section};
    database->rules_.reserve(inflection_section.count);
    for (std::uint32_t ordinal = 0; ordinal < inflection_section.count;
         ++ordinal) {
        const auto packed = inflection_records.read_u48(ordinal, 0U);
        if ((packed >> 47U) != 0U) {
            fail("reserved-bits",
                 "inflection record has nonzero reserved bits");
        }
        const auto pofs = static_cast<std::uint8_t>(packed & 0x0fU);
        const auto paradigm = static_cast<std::uint8_t>((packed >> 4U) & 0xffU);
        const auto morphology =
            static_cast<std::uint16_t>((packed >> 12U) & 0xffffU);
        const auto ending_id =
            static_cast<std::uint16_t>((packed >> 28U) & 0x01ffU);
        const auto stem_key =
            static_cast<std::uint8_t>(((packed >> 37U) & 0x03U) + 1U);
        if (ending_id >= database->ending_strings_.size()) {
            fail("invalid-reference",
                 "inflection ending string ID is out of range");
        }
        InflectionRule rule;
        rule.id = RuleId{ordinal};
        rule.part_of_speech = static_cast<PartOfSpeech>(pofs);
        rule.declension = checked_nibble(
            static_cast<std::uint8_t>(paradigm >> 4U), "rule declension");
        rule.variant = checked_nibble(
            static_cast<std::uint8_t>(paradigm & 0x0fU), "rule variant");
        rule.grammatical_case =
            static_cast<GrammaticalCase>(morphology & 0x07U);
        rule.number =
            static_cast<GrammaticalNumber>((morphology >> 3U) & 0x03U);
        rule.gender = static_cast<Gender>((morphology >> 5U) & 0x07U);
        if (rule.part_of_speech == PartOfSpeech::adjective) {
            rule.adjective_degree =
                static_cast<Degree>((morphology >> 8U) & 0x03U);
        } else if (rule.part_of_speech == PartOfSpeech::numeral) {
            rule.numeral_type =
                static_cast<NumeralType>((morphology >> 8U) & 0x07U);
        } else if (rule.part_of_speech == PartOfSpeech::adverb) {
            rule.adjective_degree = static_cast<Degree>(morphology & 0x03U);
        } else if (rule.part_of_speech == PartOfSpeech::verb) {
            rule.tense = static_cast<Tense>(morphology & 0x07U);
            rule.voice = static_cast<Voice>((morphology >> 3U) & 0x03U);
            rule.mood = static_cast<Mood>((morphology >> 5U) & 0x07U);
            rule.person = static_cast<std::uint8_t>((morphology >> 8U) & 0x03U);
            rule.number =
                static_cast<GrammaticalNumber>((morphology >> 10U) & 0x03U);
        } else if (rule.part_of_speech == PartOfSpeech::participle) {
            rule.tense = static_cast<Tense>((morphology >> 8U) & 0x07U);
            rule.voice = static_cast<Voice>((morphology >> 11U) & 0x03U);
            rule.mood = static_cast<Mood>((morphology >> 13U) & 0x07U);
        }
        rule.ending = StringId{ending_id};
        rule.stem_key = stem_key;
        rule.age = static_cast<std::uint8_t>((packed >> 39U) & 0x0fU);
        rule.frequency = static_cast<std::uint8_t>((packed >> 43U) & 0x0fU);
        if (rule.age > 8U || rule.frequency > 9U) {
            fail("invalid-enum",
                 "inflection metadata contains an invalid enum");
        }
        if ((pofs == std::to_underlying(PartOfSpeech::noun) ||
             pofs == std::to_underlying(PartOfSpeech::pronoun) ||
             pofs == std::to_underlying(PartOfSpeech::pack)) &&
            (std::to_underlying(rule.grammatical_case) > 7U ||
             std::to_underlying(rule.number) > 2U ||
             std::to_underlying(rule.gender) > 4U)) {
            fail("invalid-enum", "nominal inflection contains an invalid enum");
        }
        if (pofs == std::to_underlying(PartOfSpeech::adjective) &&
            (std::to_underlying(rule.grammatical_case) > 7U ||
             std::to_underlying(rule.number) > 2U ||
             std::to_underlying(rule.gender) > 4U ||
             std::to_underlying(rule.adjective_degree) > 3U ||
             (morphology >> 10U) != 0U)) {
            fail("invalid-enum",
                 "adjective inflection contains an invalid enum");
        }
        if (pofs == std::to_underlying(PartOfSpeech::numeral) &&
            (std::to_underlying(rule.grammatical_case) > 7U ||
             std::to_underlying(rule.number) > 2U ||
             std::to_underlying(rule.gender) > 4U ||
             std::to_underlying(rule.numeral_type) > 4U ||
             (morphology >> 11U) != 0U)) {
            fail("invalid-enum", "numeral inflection contains an invalid enum");
        }
        if (pofs == std::to_underlying(PartOfSpeech::adverb) &&
            (std::to_underlying(rule.adjective_degree) > 3U ||
             (morphology >> 2U) != 0U)) {
            fail("invalid-enum", "adverb inflection contains an invalid enum");
        }
        if (pofs == std::to_underlying(PartOfSpeech::verb) &&
            (std::to_underlying(rule.tense) > 6U ||
             std::to_underlying(rule.voice) > 2U ||
             std::to_underlying(rule.mood) > 5U || rule.person > 3U ||
             std::to_underlying(rule.number) > 2U ||
             (morphology >> 12U) != 0U)) {
            fail("invalid-enum", "verb inflection contains an invalid enum");
        }
        if (pofs == std::to_underlying(PartOfSpeech::participle) &&
            (std::to_underlying(rule.grammatical_case) > 7U ||
             std::to_underlying(rule.number) > 2U ||
             std::to_underlying(rule.gender) > 4U ||
             std::to_underlying(rule.tense) > 6U ||
             std::to_underlying(rule.voice) > 2U ||
             std::to_underlying(rule.mood) > 5U)) {
            fail("invalid-enum",
                 "participle inflection contains an invalid enum");
        }
        if (pofs == std::to_underlying(PartOfSpeech::supine) &&
            (std::to_underlying(rule.grammatical_case) > 7U ||
             std::to_underlying(rule.number) > 2U ||
             std::to_underlying(rule.gender) > 4U ||
             (morphology >> 8U) != 0U)) {
            fail("invalid-enum", "supine inflection contains an invalid enum");
        }
        if (pofs == std::to_underlying(PartOfSpeech::preposition) &&
            (std::to_underlying(rule.grammatical_case) > 7U ||
             (morphology >> 3U) != 0U)) {
            fail("invalid-enum",
                 "preposition inflection contains an invalid case");
        }
        if ((pofs == std::to_underlying(PartOfSpeech::conjunction) ||
             pofs == std::to_underlying(PartOfSpeech::interjection)) &&
            morphology != 0U) {
            fail("reserved-bits", "invariable inflection payload is nonzero");
        }
        database->rules_.push_back(rule);
    }

    if (inflection_quantity_section != nullptr) {
        database->inflection_quantities_ = parse_inflection_quantities(
            owned_bytes, *inflection_quantity_section, database->rules_,
            database->ending_strings_);
    }

    if (stem_quantity_section != nullptr) {
        const auto parsed =
            parse_stem_quantities(owned_bytes, *stem_quantity_section,
                                  database->lexemes_, database->stem_strings_);
        database->stem_quantities_.reserve(parsed.size());
        for (const auto &quantity : parsed) {
            database->stem_quantities_.push_back(
                {quantity.key, quantity.quantity});
        }
    }

    struct IndexedRule final {
        std::string_view key;
        RuleId id;
    };
    std::vector<IndexedRule> indexed_rules;
    indexed_rules.reserve(database->rules_.size());
    for (const auto &rule : database->rules_) {
        indexed_rules.push_back(
            {database->ending_string(rule.ending), rule.id});
    }
    std::ranges::sort(indexed_rules,
                      [](const IndexedRule &left, const IndexedRule &right) {
                          const auto key_order =
                              normalized_compare(left.key, right.key);
                          if (key_order != 0) {
                              return key_order < 0;
                          }
                          return left.id < right.id;
                      });
    database->ending_rule_ids_.reserve(indexed_rules.size());
    for (std::size_t index = 0; index < indexed_rules.size();) {
        const auto first = database->ending_rule_ids_.size();
        const auto key = indexed_rules[index].key;
        do {
            database->ending_rule_ids_.push_back(indexed_rules[index].id);
            ++index;
        } while (index < indexed_rules.size() &&
                 normalized_equal(key, indexed_rules[index].key));
        database->ending_groups_.push_back({
            key,
            static_cast<std::uint32_t>(first),
            static_cast<std::uint32_t>(database->ending_rule_ids_.size() -
                                       first),
        });
    }

    struct IndexedUnique final {
        std::string_view key;
        UniqueReference reference;
    };
    std::vector<IndexedUnique> indexed_uniques;
    indexed_uniques.reserve(unique_section.count);
    const RecordView unique_records{section_bytes(owned_bytes, unique_section),
                                    unique_section};
    for (std::uint32_t ordinal = 0; ordinal < unique_section.count; ++ordinal) {
        const auto surface_id = unique_records.read_u16(ordinal, 0U);
        const auto meaning_id = content == DatabaseContent::full
                                    ? unique_records.read_u16(ordinal, 2U)
                                    : 0U;
        const auto metadata = unique_records.read_u64(
            ordinal, content == DatabaseContent::full ? 4U : 2U);
        if (surface_id >= database->stem_strings_.size() ||
            (content == DatabaseContent::full &&
             meaning_id >= database->meaning_strings_.size())) {
            fail("invalid-reference",
                 "unique string or meaning ID is out of range");
        }
        if ((metadata >> 50U) != 0U) {
            fail("reserved-bits", "unique record has nonzero reserved bits");
        }

        const auto part = static_cast<PartOfSpeech>(metadata & 0x0fU);
        const auto paradigm =
            static_cast<std::uint8_t>((metadata >> 4U) & 0xffU);
        const auto morphology =
            static_cast<std::uint16_t>((metadata >> 12U) & 0xffffU);
        const auto translation =
            static_cast<std::uint32_t>((metadata >> 28U) & 0x3f'ffffU);

        LexemeRecord lexeme;
        lexeme.stems.fill(StringId{surface_id});
        lexeme.meaning = StringId{meaning_id};
        lexeme.dictionary = DictionaryKind::unique;
        lexeme.dictionary_entry = ordinal;
        lexeme.part_of_speech = part;
        lexeme.declension = checked_nibble(
            static_cast<std::uint8_t>(paradigm >> 4U), "unique declension");
        lexeme.variant = checked_nibble(
            static_cast<std::uint8_t>(paradigm & 0x0fU), "unique variant");
        lexeme.age = static_cast<std::uint8_t>(translation & 0x0fU);
        lexeme.subject = static_cast<std::uint8_t>((translation >> 4U) & 0x0fU);
        lexeme.geography =
            static_cast<std::uint8_t>((translation >> 8U) & 0x1fU);
        lexeme.frequency =
            static_cast<std::uint8_t>((translation >> 13U) & 0x0fU);
        lexeme.source = static_cast<std::uint8_t>((translation >> 17U) & 0x1fU);
        if (lexeme.age > 8U || lexeme.subject > 11U || lexeme.geography > 17U ||
            lexeme.frequency > 9U || lexeme.source > 25U) {
            fail("invalid-enum",
                 "unique lexical metadata contains an invalid enum");
        }

        Morphology decoded;
        const auto grammatical_case =
            static_cast<GrammaticalCase>(morphology & 0x07U);
        const auto number =
            static_cast<GrammaticalNumber>((morphology >> 3U) & 0x03U);
        const auto gender = static_cast<Gender>((morphology >> 5U) & 0x07U);
        if (part == PartOfSpeech::noun || part == PartOfSpeech::pronoun) {
            if (std::to_underlying(grammatical_case) > 7U ||
                std::to_underlying(number) > 2U ||
                std::to_underlying(gender) > 4U || (morphology >> 8U) != 0U) {
                fail("invalid-enum",
                     "unique nominal morphology contains an invalid enum");
            }
            if (part == PartOfSpeech::noun) {
                decoded = NounMorphology{lexeme.declension, lexeme.variant,
                                         grammatical_case, number, gender};
            } else {
                decoded = PronounMorphology{lexeme.declension, lexeme.variant,
                                            grammatical_case, number, gender};
            }
        } else if (part == PartOfSpeech::adjective) {
            const auto degree = static_cast<Degree>((morphology >> 8U) & 0x03U);
            if (std::to_underlying(grammatical_case) > 7U ||
                std::to_underlying(number) > 2U ||
                std::to_underlying(gender) > 4U ||
                std::to_underlying(degree) > 3U || (morphology >> 10U) != 0U) {
                fail("invalid-enum",
                     "unique adjective morphology contains an invalid enum");
            }
            decoded = AdjectiveMorphology{lexeme.declension,
                                          lexeme.variant,
                                          grammatical_case,
                                          number,
                                          gender,
                                          degree};
        } else if (part == PartOfSpeech::verb) {
            const auto tense = static_cast<Tense>(morphology & 0x07U);
            const auto voice = static_cast<Voice>((morphology >> 3U) & 0x03U);
            const auto mood = static_cast<Mood>((morphology >> 5U) & 0x07U);
            const auto person =
                static_cast<std::uint8_t>((morphology >> 8U) & 0x03U);
            const auto verb_number =
                static_cast<GrammaticalNumber>((morphology >> 10U) & 0x03U);
            if (std::to_underlying(tense) > 6U ||
                std::to_underlying(voice) > 2U ||
                std::to_underlying(mood) > 5U || person > 3U ||
                std::to_underlying(verb_number) > 2U ||
                (morphology >> 12U) != 0U) {
                fail("invalid-enum",
                     "unique verb morphology contains an invalid enum");
            }
            decoded = VerbMorphology{
                lexeme.declension, lexeme.variant, tense, voice, mood, person,
                verb_number};
        } else {
            fail("invalid-enum", "unsupported unique part of speech");
        }

        const auto lexeme_id =
            LexemeId{static_cast<std::uint32_t>(database->lexemes_.size())};
        database->lexemes_.push_back(lexeme);
        indexed_uniques.push_back(
            {database->stem_string(StringId{surface_id}),
             UniqueReference{lexeme_id, std::move(decoded)}});
    }

    std::ranges::sort(indexed_uniques, [](const IndexedUnique &left,
                                         const IndexedUnique &right) {
        const auto key_order = normalized_compare(left.key, right.key);
        if (key_order != 0) {
            return key_order < 0;
        }
        return left.reference.lexeme < right.reference.lexeme;
    });
    database->unique_references_.reserve(indexed_uniques.size());
    for (std::size_t index = 0; index < indexed_uniques.size();) {
        const auto first = database->unique_references_.size();
        const auto key = indexed_uniques[index].key;
        do {
            database->unique_references_.push_back(
                std::move(indexed_uniques[index].reference));
            ++index;
        } while (index < indexed_uniques.size() &&
                 normalized_equal(key, indexed_uniques[index].key));
        database->unique_groups_.push_back({
            key,
            static_cast<std::uint32_t>(first),
            static_cast<std::uint32_t>(database->unique_references_.size() -
                                       first),
        });
    }

    const RecordView suffix_records{section_bytes(owned_bytes, suffix_section),
                                    suffix_section};
    database->suffixes_.reserve(suffix_section.count);
    for (std::uint32_t ordinal = 0; ordinal < suffix_section.count; ++ordinal) {
        const auto addon_id = suffix_records.read_u16(ordinal, 0U);
        const auto fix_id = suffix_records.read_u16(ordinal, 2U);
        const auto meaning_id = content == DatabaseContent::full
                                    ? suffix_records.read_u16(ordinal, 4U)
                                    : 0U;
        const auto metadata = suffix_records.read_u64(
            ordinal, content == DatabaseContent::full ? 6U : 4U);
        if (fix_id >= database->suffix_strings_.size() ||
            (content == DatabaseContent::full &&
             meaning_id >= database->suffix_meanings_.size())) {
            fail("invalid-reference",
                 "suffix string or meaning ID is out of range");
        }
        if ((metadata >> 46U) != 0U) {
            fail("reserved-bits", "suffix record has nonzero reserved bits");
        }

        SuffixRule suffix;
        suffix.id = AddonId{addon_id};
        suffix.fix = SuffixStringId{fix_id};
        suffix.meaning = SuffixMeaningId{meaning_id};
        suffix.root = static_cast<PartOfSpeech>(metadata & 0x0fU);
        suffix.root_key = static_cast<std::uint8_t>((metadata >> 4U) & 0x07U);
        suffix.target = static_cast<PartOfSpeech>((metadata >> 7U) & 0x0fU);
        suffix.target_key =
            static_cast<std::uint8_t>((metadata >> 11U) & 0x07U);
        const auto paradigm =
            static_cast<std::uint8_t>((metadata >> 14U) & 0xffU);
        suffix.target_declension =
            checked_nibble(static_cast<std::uint8_t>(paradigm >> 4U),
                           "suffix target declension");
        suffix.target_variant =
            checked_nibble(static_cast<std::uint8_t>(paradigm & 0x0fU),
                           "suffix target variant");
        const auto attribute_0 =
            static_cast<std::uint8_t>((metadata >> 22U) & 0x0fU);
        suffix.target_attribute = attribute_0;
        suffix.target_noun_kind =
            static_cast<std::uint8_t>((metadata >> 26U) & 0x0fU);
        suffix.numeric_value =
            static_cast<std::uint8_t>((metadata >> 30U) & 0xffU);
        const auto connector =
            static_cast<std::uint8_t>((metadata >> 38U) & 0xffU);
        suffix.connector = static_cast<char>(connector);

        if (std::to_underlying(suffix.root) >
                std::to_underlying(PartOfSpeech::verb) ||
            std::to_underlying(suffix.target) >
                std::to_underlying(PartOfSpeech::verb) ||
            suffix.root_key > 4U || suffix.target_key > 4U ||
            connector > 0x7fU) {
            fail("invalid-enum", "suffix metadata contains an invalid enum");
        }
        if (suffix.target == PartOfSpeech::noun) {
            suffix.target_gender = static_cast<Gender>(attribute_0);
            if (attribute_0 > std::to_underlying(Gender::common) ||
                suffix.target_noun_kind > 9U) {
                fail("invalid-enum", "noun suffix target is invalid");
            }
        } else if (suffix.target == PartOfSpeech::adjective) {
            suffix.target_degree = static_cast<Degree>(attribute_0);
            if (attribute_0 > std::to_underlying(Degree::superlative) ||
                suffix.target_noun_kind != 0U || suffix.numeric_value != 0U) {
                fail("invalid-enum", "adjective suffix target is invalid");
            }
        } else if (suffix.target == PartOfSpeech::numeral) {
            suffix.target_numeral_type = static_cast<NumeralType>(attribute_0);
            if (attribute_0 > std::to_underlying(NumeralType::adverbial) ||
                suffix.target_noun_kind != 0U) {
                fail("invalid-enum", "numeral suffix target is invalid");
            }
        } else if (suffix.target == PartOfSpeech::adverb) {
            suffix.target_degree = static_cast<Degree>(attribute_0);
            if (attribute_0 > std::to_underlying(Degree::superlative) ||
                suffix.target_noun_kind != 0U || suffix.numeric_value != 0U) {
                fail("invalid-enum", "adverb suffix target is invalid");
            }
        } else if (suffix.target == PartOfSpeech::verb) {
            if (attribute_0 != 0U || suffix.target_noun_kind != 0U ||
                suffix.numeric_value != 0U) {
                fail("invalid-enum", "verb suffix target is invalid");
            }
        } else {
            fail("invalid-enum", "unsupported suffix target class");
        }
        database->maximum_suffix_size_ =
            std::max(database->maximum_suffix_size_,
                     database->suffix_string(suffix.fix).size());
        database->suffixes_.push_back(suffix);
    }

    struct IndexedSuffix final {
        std::string_view key;
        AddonId id;
    };
    std::vector<IndexedSuffix> indexed_suffixes;
    indexed_suffixes.reserve(database->suffixes_.size());
    for (const auto &suffix : database->suffixes_) {
        indexed_suffixes.push_back(
            {database->suffix_string(suffix.fix), suffix.id});
    }
    std::ranges::sort(indexed_suffixes, [](const IndexedSuffix &left,
                                          const IndexedSuffix &right) {
        const auto key_order = normalized_compare(left.key, right.key);
        if (key_order != 0) {
            return key_order < 0;
        }
        return left.id < right.id;
    });
    database->suffix_ids_.reserve(indexed_suffixes.size());
    for (std::size_t index = 0; index < indexed_suffixes.size();) {
        const auto first = database->suffix_ids_.size();
        const auto key = indexed_suffixes[index].key;
        do {
            database->suffix_ids_.push_back(indexed_suffixes[index].id);
            ++index;
        } while (index < indexed_suffixes.size() &&
                 normalized_equal(key, indexed_suffixes[index].key));
        database->suffix_groups_.push_back({
            key,
            static_cast<std::uint32_t>(first),
            static_cast<std::uint32_t>(database->suffix_ids_.size() - first),
        });
    }

    const RecordView prefix_records{section_bytes(owned_bytes, prefix_section),
                                    prefix_section};
    database->prefixes_.reserve(prefix_section.count);
    for (std::uint32_t ordinal = 0; ordinal < prefix_section.count; ++ordinal) {
        const auto addon_id = prefix_records.read_u16(ordinal, 0U);
        const auto fix_id = prefix_records.read_u16(ordinal, 2U);
        const auto meaning_id = content == DatabaseContent::full
                                    ? prefix_records.read_u16(ordinal, 4U)
                                    : 0U;
        const auto metadata = prefix_records.read_u16(
            ordinal, content == DatabaseContent::full ? 6U : 4U);
        if (fix_id >= database->prefix_strings_.size() ||
            (content == DatabaseContent::full &&
             meaning_id >= database->prefix_meanings_.size())) {
            fail("invalid-reference",
                 "prefix string or meaning ID is out of range");
        }

        PrefixRule prefix;
        prefix.id = AddonId{addon_id};
        prefix.fix = PrefixStringId{fix_id};
        prefix.meaning = PrefixMeaningId{meaning_id};
        prefix.root = static_cast<PartOfSpeech>(metadata & 0x0fU);
        prefix.target = static_cast<PartOfSpeech>((metadata >> 4U) & 0x0fU);
        const auto connector = static_cast<std::uint8_t>(metadata >> 8U);
        prefix.connector = static_cast<char>(connector);
        if (std::to_underlying(prefix.root) >
                std::to_underlying(PartOfSpeech::verb) ||
            prefix.root != prefix.target || connector > 0x7fU) {
            fail("invalid-enum", "prefix metadata contains an invalid enum");
        }
        database->prefixes_.push_back(prefix);
    }

    const RecordView tackon_records{section_bytes(owned_bytes, tackon_section),
                                    tackon_section};
    database->tackons_.reserve(tackon_section.count);
    for (std::uint32_t ordinal = 0; ordinal < tackon_section.count; ++ordinal) {
        const auto addon_id = tackon_records.read_u16(ordinal, 0U);
        const auto fix_id = tackon_records.read_u16(ordinal, 2U);
        const auto meaning_id = content == DatabaseContent::full
                                    ? tackon_records.read_u16(ordinal, 4U)
                                    : 0U;
        const auto metadata = tackon_records.read_u32(
            ordinal, content == DatabaseContent::full ? 6U : 4U);
        if (fix_id >= database->tackon_strings_.size() ||
            (content == DatabaseContent::full &&
             meaning_id >= database->tackon_meanings_.size())) {
            fail("invalid-reference",
                 "tackon string or meaning ID is out of range");
        }
        if ((metadata >> 22U) != 0U) {
            fail("reserved-bits", "tackon record has nonzero reserved bits");
        }

        TackonRule tackon;
        tackon.id = AddonId{addon_id};
        tackon.fix = TackonStringId{fix_id};
        tackon.meaning = TackonMeaningId{meaning_id};
        tackon.base = static_cast<PartOfSpeech>(metadata & 0x0fU);
        const auto paradigm =
            static_cast<std::uint8_t>((metadata >> 4U) & 0xffU);
        tackon.declension = checked_nibble(
            static_cast<std::uint8_t>(paradigm >> 4U), "tackon declension");
        tackon.variant = checked_nibble(
            static_cast<std::uint8_t>(paradigm & 0x0fU), "tackon variant");
        const auto attribute_0 =
            static_cast<std::uint8_t>((metadata >> 12U) & 0x0fU);
        tackon.noun_kind = static_cast<std::uint8_t>((metadata >> 16U) & 0x0fU);
        tackon.packon = ((metadata >> 20U) & 1U) != 0U;
        tackon.enclitic = ((metadata >> 21U) & 1U) != 0U;
        if (std::to_underlying(tackon.base) >
            std::to_underlying(PartOfSpeech::adjective)) {
            fail("invalid-enum", "tackon base class is invalid");
        }
        if (tackon.base == PartOfSpeech::noun) {
            tackon.gender = static_cast<Gender>(attribute_0);
            if (attribute_0 > std::to_underlying(Gender::common) ||
                tackon.noun_kind > 9U) {
                fail("invalid-enum", "noun tackon target is invalid");
            }
        } else if (tackon.base == PartOfSpeech::pronoun ||
                   tackon.base == PartOfSpeech::pack) {
            tackon.pronoun_kind = static_cast<PronounKind>(attribute_0);
            if (attribute_0 > std::to_underlying(PronounKind::adjectival) ||
                tackon.noun_kind != 0U) {
                fail("invalid-enum", "pronoun tackon target is invalid");
            }
        } else if (tackon.base == PartOfSpeech::adjective) {
            tackon.adjective_degree = static_cast<Degree>(attribute_0);
            if (attribute_0 > std::to_underlying(Degree::superlative) ||
                tackon.noun_kind != 0U) {
                fail("invalid-enum", "adjective tackon target is invalid");
            }
        } else if (attribute_0 != 0U || tackon.noun_kind != 0U ||
                   tackon.declension != 0U || tackon.variant != 0U) {
            fail("invalid-enum", "generic tackon target is not empty");
        }
        if (tackon.packon != (tackon.base == PartOfSpeech::pack) ||
            (tackon.packon && tackon.enclitic)) {
            fail("invalid-enum", "tackon classification is inconsistent");
        }
        database->maximum_tackon_size_ =
            std::max(database->maximum_tackon_size_,
                     database->tackon_string(tackon.fix).size());
        database->tackons_.push_back(tackon);
    }

    database->addon_references_.resize(database->prefixes_.size() +
                                       database->suffixes_.size() +
                                       database->tackons_.size());
    const auto register_addon = [&](const AddonId id, const AddonKind kind,
                                    const std::uint32_t ordinal) {
        if (id.value() >= database->addon_references_.size() ||
            database->addon_references_[id.value()].kind !=
                AddonKind::unknown) {
            fail("invalid-reference", "addon ID is not dense and unique");
        }
        database->addon_references_[id.value()] = {kind, ordinal};
    };
    for (std::uint32_t ordinal = 0; ordinal < database->prefixes_.size();
         ++ordinal) {
        const auto kind =
            database->prefixes_[ordinal].root == PartOfSpeech::pack
                ? AddonKind::tickon
                : AddonKind::prefix;
        register_addon(database->prefixes_[ordinal].id, kind, ordinal);
    }
    for (std::uint32_t ordinal = 0; ordinal < database->suffixes_.size();
         ++ordinal) {
        register_addon(database->suffixes_[ordinal].id, AddonKind::suffix,
                       ordinal);
    }
    for (std::uint32_t ordinal = 0; ordinal < database->tackons_.size();
         ++ordinal) {
        register_addon(database->tackons_[ordinal].id,
                       database->tackons_[ordinal].packon ? AddonKind::packon
                                                          : AddonKind::tackon,
                       ordinal);
    }
    if (std::ranges::any_of(database->addon_references_,
                            [](const AddonReference &reference) {
                                return reference.kind == AddonKind::unknown;
                            })) {
        fail("invalid-reference", "addon ID namespace contains a gap");
    }
    for (const auto &lexeme : database->lexemes_) {
        if (lexeme.required_packon &&
            (lexeme.required_packon->value() >=
                 database->addon_references_.size() ||
             database->addon_references_[lexeme.required_packon->value()].kind !=
                 AddonKind::packon)) {
            fail("invalid-reference",
                 "lexeme packon metadata does not name a packon addon");
        }
    }

    struct IndexedPrefix final {
        std::string_view key;
        AddonId id;
    };
    std::vector<IndexedPrefix> indexed_prefixes;
    std::vector<IndexedPrefix> indexed_tickons;
    indexed_prefixes.reserve(database->prefixes_.size());
    for (const auto &prefix : database->prefixes_) {
        const auto key = database->prefix_string(prefix.fix);
        database->maximum_prefix_size_ =
            std::max(database->maximum_prefix_size_, key.size());
        // PACK prefixes are tickons in the Ada loader and belong to their own
        // scheduler rather than ordinary prefix derivation.
        if (prefix.root == PartOfSpeech::pack) {
            indexed_tickons.push_back({key, prefix.id});
            continue;
        }
        indexed_prefixes.push_back({key, prefix.id});
    }
    std::ranges::sort(indexed_prefixes, [](const IndexedPrefix &left,
                                          const IndexedPrefix &right) {
        const auto key_order = normalized_compare(left.key, right.key);
        if (key_order != 0) {
            return key_order < 0;
        }
        return left.id < right.id;
    });
    database->prefix_ids_.reserve(indexed_prefixes.size());
    for (std::size_t index = 0; index < indexed_prefixes.size();) {
        const auto first = database->prefix_ids_.size();
        const auto key = indexed_prefixes[index].key;
        do {
            database->prefix_ids_.push_back(indexed_prefixes[index].id);
            ++index;
        } while (index < indexed_prefixes.size() &&
                 normalized_equal(key, indexed_prefixes[index].key));
        database->prefix_groups_.push_back({
            key,
            static_cast<std::uint32_t>(first),
            static_cast<std::uint32_t>(database->prefix_ids_.size() - first),
        });
    }

    std::ranges::sort(indexed_tickons, [](const IndexedPrefix &left,
                                         const IndexedPrefix &right) {
        const auto key_order = normalized_compare(left.key, right.key);
        if (key_order != 0) {
            return key_order < 0;
        }
        return left.id < right.id;
    });
    for (std::size_t index = 0; index < indexed_tickons.size();) {
        const auto first = database->tickon_ids_.size();
        const auto key = indexed_tickons[index].key;
        do {
            database->tickon_ids_.push_back(indexed_tickons[index].id);
            ++index;
        } while (index < indexed_tickons.size() &&
                 normalized_equal(key, indexed_tickons[index].key));
        database->tickon_groups_.push_back({
            key,
            static_cast<std::uint32_t>(first),
            static_cast<std::uint32_t>(database->tickon_ids_.size() - first),
        });
    }

    struct IndexedTackon final {
        std::string_view key;
        AddonId id;
    };
    std::vector<IndexedTackon> indexed_tackons;
    std::vector<IndexedTackon> indexed_packons;
    for (const auto &tackon : database->tackons_) {
        auto &target = tackon.packon ? indexed_packons : indexed_tackons;
        target.push_back({database->tackon_string(tackon.fix), tackon.id});
    }
    const auto build_tackon_index = [](auto &source, auto &ids, auto &groups) {
        std::ranges::sort(
            source, [](const IndexedTackon &left, const IndexedTackon &right) {
                const auto key_order =
                    normalized_compare(left.key, right.key);
                if (key_order != 0) {
                    return key_order < 0;
                }
                return left.id < right.id;
            });
        for (std::size_t index = 0; index < source.size();) {
            const auto first = ids.size();
            const auto key = source[index].key;
            do {
                ids.push_back(source[index].id);
                ++index;
            } while (index < source.size() &&
                     normalized_equal(key, source[index].key));
            groups.push_back({key, static_cast<std::uint32_t>(first),
                              static_cast<std::uint32_t>(ids.size() - first)});
        }
    };
    build_tackon_index(indexed_tackons, database->tackon_ids_,
                       database->tackon_groups_);
    build_tackon_index(indexed_packons, database->packon_ids_,
                       database->packon_groups_);

    const auto stem_boundaries =
        section_bytes(owned_bytes, stem_boundary_section);
    const auto inflection_boundaries =
        section_bytes(owned_bytes, inflection_boundary_section);
    const auto validate_boundaries = [](const std::span<const std::byte> values,
                                        const std::uint32_t expected_last) {
        std::uint16_t previous = 0;
        for (std::size_t offset = 0; offset < values.size(); offset += 2U) {
            const auto current = read_u16_le(values, offset);
            if (current < previous) {
                fail("invalid-boundaries", "PoC boundaries are not monotonic");
            }
            previous = current;
        }
        if (previous != expected_last) {
            fail("invalid-boundaries",
                 "PoC boundary section does not cover its records");
        }
    };
    validate_boundaries(stem_boundaries, stem_reference_section.count);
    validate_boundaries(inflection_boundaries, inflection_section.count);

    return std::unique_ptr<const Database>{std::move(database)};
} catch (const LoadFailure &error) {
    return std::unexpected(LoadError{error.code(), error.what()});
} catch (const std::bad_alloc &) {
    return std::unexpected(
        LoadError{"out-of-memory", "cannot allocate WWDB snapshot"});
}

std::span<const StemReference>
Database::lookup_stem(const std::string_view normalized_ascii) const noexcept {
    const auto found = std::ranges::lower_bound(
        stem_groups_, normalized_ascii,
        [](const std::string_view left, const std::string_view right) {
            return normalized_less(left, right);
        },
        &StemGroup::key);
    if (found == stem_groups_.end() ||
        !normalized_equal(found->key, normalized_ascii)) {
        return {};
    }
    return std::span<const StemReference>{stem_references_}.subspan(
        found->first, found->count);
}

std::span<const RuleId> Database::lookup_ending(
    const std::string_view normalized_ascii) const noexcept {
    const auto found = std::ranges::lower_bound(
        ending_groups_, normalized_ascii,
        [](const std::string_view left, const std::string_view right) {
            return normalized_less(left, right);
        },
        &EndingGroup::key);
    if (found == ending_groups_.end() ||
        !normalized_equal(found->key, normalized_ascii)) {
        return {};
    }
    return std::span<const RuleId>{ending_rule_ids_}.subspan(found->first,
                                                             found->count);
}

std::span<const UniqueReference> Database::lookup_unique(
    const std::string_view normalized_ascii) const noexcept {
    const auto found = std::ranges::lower_bound(
        unique_groups_, normalized_ascii,
        [](const std::string_view left, const std::string_view right) {
            return normalized_less(left, right);
        },
        &UniqueGroup::key);
    if (found == unique_groups_.end() ||
        !normalized_equal(found->key, normalized_ascii)) {
        return {};
    }
    return std::span<const UniqueReference>{unique_references_}.subspan(
        found->first, found->count);
}

std::span<const AddonId> Database::lookup_suffix(
    const std::string_view normalized_ascii) const noexcept {
    const auto found = std::ranges::lower_bound(
        suffix_groups_, normalized_ascii,
        [](const std::string_view left, const std::string_view right) {
            return normalized_less(left, right);
        },
        &SuffixGroup::key);
    if (found == suffix_groups_.end() ||
        !normalized_equal(found->key, normalized_ascii)) {
        return {};
    }
    return std::span<const AddonId>{suffix_ids_}.subspan(found->first,
                                                         found->count);
}

std::span<const AddonId> Database::lookup_prefix(
    const std::string_view normalized_ascii) const noexcept {
    const auto found = std::ranges::lower_bound(
        prefix_groups_, normalized_ascii,
        [](const std::string_view left, const std::string_view right) {
            return normalized_less(left, right);
        },
        &PrefixGroup::key);
    if (found == prefix_groups_.end() ||
        !normalized_equal(found->key, normalized_ascii)) {
        return {};
    }
    return std::span<const AddonId>{prefix_ids_}.subspan(found->first,
                                                         found->count);
}

std::span<const AddonId> Database::lookup_tickon(
    const std::string_view normalized_ascii) const noexcept {
    const auto found = std::ranges::lower_bound(
        tickon_groups_, normalized_ascii,
        [](const std::string_view left, const std::string_view right) {
            return normalized_less(left, right);
        },
        &PrefixGroup::key);
    if (found == tickon_groups_.end() ||
        !normalized_equal(found->key, normalized_ascii)) {
        return {};
    }
    return std::span<const AddonId>{tickon_ids_}.subspan(found->first,
                                                         found->count);
}

std::span<const AddonId> Database::lookup_tackon(
    const std::string_view normalized_ascii) const noexcept {
    const auto found = std::ranges::lower_bound(
        tackon_groups_, normalized_ascii,
        [](const std::string_view left, const std::string_view right) {
            return normalized_less(left, right);
        },
        &TackonGroup::key);
    if (found == tackon_groups_.end() ||
        !normalized_equal(found->key, normalized_ascii)) {
        return {};
    }
    return std::span<const AddonId>{tackon_ids_}.subspan(found->first,
                                                         found->count);
}

std::span<const AddonId> Database::lookup_packon(
    const std::string_view normalized_ascii) const noexcept {
    const auto found = std::ranges::lower_bound(
        packon_groups_, normalized_ascii,
        [](const std::string_view left, const std::string_view right) {
            return normalized_less(left, right);
        },
        &TackonGroup::key);
    if (found == packon_groups_.end() ||
        !normalized_equal(found->key, normalized_ascii)) {
        return {};
    }
    return std::span<const AddonId>{packon_ids_}.subspan(found->first,
                                                         found->count);
}

const LexemeRecord &Database::lexeme(const LexemeId id) const {
    return lexemes_.at(id.value());
}

const InflectionRule &Database::rule(const RuleId id) const {
    return rules_.at(id.value());
}

QuantityMask Database::inflection_quantity(const RuleId id) const noexcept {
    const auto ordinal = static_cast<std::size_t>(id.value());
    return ordinal < inflection_quantities_.size()
               ? inflection_quantities_[ordinal]
               : QuantityMask{};
}

QuantityMask
Database::stem_quantity(const LexemeId id,
                        const std::uint8_t lexical_slot) const noexcept {
    if (lexical_slot >= stem_quantity_slot_count) {
        return {};
    }
    const auto key =
        static_cast<std::uint32_t>(id.value()) |
        (static_cast<std::uint32_t>(lexical_slot) << stem_quantity_slot_shift);
    const auto found = std::ranges::lower_bound(stem_quantities_, key, {},
                                                &StemQuantityRecord::key);
    return found != stem_quantities_.end() && found->key == key
               ? found->quantity
               : QuantityMask{};
}

const SuffixRule &Database::suffix(const AddonId id) const {
    const auto &reference = addon_references_.at(id.value());
    if (reference.kind != AddonKind::suffix) {
        throw std::out_of_range{"addon ID does not designate a suffix"};
    }
    return suffixes_.at(reference.ordinal);
}

const PrefixRule &Database::prefix(const AddonId id) const {
    const auto &reference = addon_references_.at(id.value());
    if (reference.kind != AddonKind::prefix &&
        reference.kind != AddonKind::tickon) {
        throw std::out_of_range{"addon ID does not designate a prefix"};
    }
    return prefixes_.at(reference.ordinal);
}

const TackonRule &Database::tackon(const AddonId id) const {
    const auto &reference = addon_references_.at(id.value());
    if (reference.kind != AddonKind::tackon &&
        reference.kind != AddonKind::packon) {
        throw std::out_of_range{"addon ID does not designate a tackon"};
    }
    return tackons_.at(reference.ordinal);
}

const RewriteRule &Database::rewrite(const RewriteId id) const {
    return rewrites_.at(id.value());
}

AddonKind Database::addon_kind(const AddonId id) const {
    return addon_references_.at(id.value()).kind;
}

std::string_view Database::stem_string(const StringId id) const {
    return stem_strings_.at(id.value());
}

std::string_view Database::meaning(const StringId id) const {
    return meaning_strings_.at(id.value());
}

std::string_view Database::ending_string(const StringId id) const {
    return ending_strings_.at(id.value());
}

std::string_view Database::suffix_string(const SuffixStringId id) const {
    return suffix_strings_.at(id.value());
}

std::string_view Database::suffix_meaning(const SuffixMeaningId id) const {
    return suffix_meanings_.at(id.value());
}

std::string_view Database::prefix_string(const PrefixStringId id) const {
    return prefix_strings_.at(id.value());
}

std::string_view Database::prefix_meaning(const PrefixMeaningId id) const {
    return prefix_meanings_.at(id.value());
}

std::string_view Database::tackon_string(const TackonStringId id) const {
    return tackon_strings_.at(id.value());
}

std::string_view Database::tackon_meaning(const TackonMeaningId id) const {
    return tackon_meanings_.at(id.value());
}

std::string_view Database::rewrite_string(const RewriteStringId id) const {
    return rewrite_strings_.at(id.value());
}

std::string_view Database::rewrite_meaning(const RewriteMeaningId id) const {
    return rewrite_meanings_.at(id.value());
}

} // namespace words
