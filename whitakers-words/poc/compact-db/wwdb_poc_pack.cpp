// Quick proof of concept for the compact layout described in
// docs/auditoria-binarios-e-formato-compacto.md.
//
// This is deliberately a measurement tool, not the production wwpack. It
// reads the concrete GNAT/x86-64 legacy files in this repository and writes a
// portable, explicitly little-endian image. Version 1.8 retains the vowel
// quantity masks from 1.7 and adds a typed PACKON selector, which lets the
// search-only projection operate without inspecting editorial meanings.
// This tool still is not the production wwpack.

#include <algorithm>
#include <array>
#include <bit>
#include <charconv>
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

namespace {

using Bytes = std::vector<std::byte>;

constexpr std::size_t dictionary_record_size = 180;
constexpr std::size_t stem_record_size = 56;
constexpr std::size_t inflection_record_size = 40;
constexpr std::size_t inflections_per_section = 570;
constexpr std::size_t inflection_section_count = 5;

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

enum class PackingProfile : std::uint32_t {
    simple = 1,
    dense = 2,
    columnar = 3,
    search_only = 4,
};

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

struct QuantitySources final {
    std::vector<InflectionQuantitySource> inflections;
    std::vector<StemQuantitySource> stems;
};

std::uint8_t pack_paradigm(std::uint32_t which, std::uint32_t variant);

[[noreturn]] void fail(std::string message) {
    throw std::runtime_error(std::move(message));
}

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
    if (offset > bytes.size() || bytes.size() - offset < 4) {
        fail("legacy u32 read past end of buffer");
    }
    return static_cast<std::uint32_t>(byte_at(bytes, offset)) |
           (static_cast<std::uint32_t>(byte_at(bytes, offset + 1)) << 8U) |
           (static_cast<std::uint32_t>(byte_at(bytes, offset + 2)) << 16U) |
           (static_cast<std::uint32_t>(byte_at(bytes, offset + 3)) << 24U);
}

std::uint64_t read_u64_le(std::span<const std::byte> bytes,
                          std::size_t offset) {
    const auto low = read_u32_le(bytes, offset);
    const auto high = read_u32_le(bytes, offset + 4);
    return static_cast<std::uint64_t>(low) |
           (static_cast<std::uint64_t>(high) << 32U);
}

void append_u8(Bytes &output, std::uint8_t value) {
    output.push_back(static_cast<std::byte>(value));
}

void append_u16_le(Bytes &output, std::uint16_t value) {
    append_u8(output, static_cast<std::uint8_t>(value));
    append_u8(output, static_cast<std::uint8_t>(value >> 8U));
}

void append_u24_le(Bytes &output, std::uint32_t value) {
    if (value > 0x00ff'ffffU) {
        fail("u24 value is out of range");
    }
    append_u8(output, static_cast<std::uint8_t>(value));
    append_u8(output, static_cast<std::uint8_t>(value >> 8U));
    append_u8(output, static_cast<std::uint8_t>(value >> 16U));
}

void append_u32_le(Bytes &output, std::uint32_t value) {
    append_u8(output, static_cast<std::uint8_t>(value));
    append_u8(output, static_cast<std::uint8_t>(value >> 8U));
    append_u8(output, static_cast<std::uint8_t>(value >> 16U));
    append_u8(output, static_cast<std::uint8_t>(value >> 24U));
}

void append_u48_le(Bytes &output, std::uint64_t value) {
    if (value > 0x0000'ffff'ffff'ffffULL) {
        fail("u48 value is out of range");
    }
    append_u32_le(output, static_cast<std::uint32_t>(value));
    append_u16_le(output, static_cast<std::uint16_t>(value >> 32U));
}

void append_u64_le(Bytes &output, std::uint64_t value) {
    append_u32_le(output, static_cast<std::uint32_t>(value));
    append_u32_le(output, static_cast<std::uint32_t>(value >> 32U));
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
    if (header.size() < 2 || header.size() > 3 || target.size() < 5) {
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
    case 1: // noun
        if (target.size() != 8) {
            fail("invalid noun suffix target in ADDONS.LAT");
        }
        result.paradigm = paradigm(3, 4);
        result.attribute_0 =
            enum_value(target[5], {"X", "M", "F", "N", "C"}, "gender");
        result.attribute_1 = enum_value(
            target[6], {"x", "s", "m", "a", "g", "n", "p", "t", "l", "w"},
            "noun kind");
        result.target_key = parse_u8(target[7], "suffix target key");
        break;
    case 4: // adjective
        if (target.size() != 7) {
            fail("invalid adjective suffix target in ADDONS.LAT");
        }
        result.paradigm = paradigm(3, 4);
        result.attribute_0 =
            enum_value(target[5], {"X", "POS", "COMP", "SUPER"}, "degree");
        result.target_key = parse_u8(target[6], "suffix target key");
        break;
    case 5: // numeral
        if (target.size() != 8) {
            fail("invalid numeral suffix target in ADDONS.LAT");
        }
        result.paradigm = paradigm(3, 4);
        result.attribute_0 = enum_value(
            target[5], {"X", "CARD", "ORD", "DIST", "ADVERB"}, "numeral sort");
        result.numeric_value = parse_u8(target[6], "numeral value");
        result.target_key = parse_u8(target[7], "suffix target key");
        break;
    case 6: // adverb
        if (target.size() != 5) {
            fail("invalid adverb suffix target in ADDONS.LAT");
        }
        result.attribute_0 =
            enum_value(target[3], {"X", "POS", "COMP", "SUPER"}, "degree");
        result.target_key = parse_u8(target[4], "suffix target key");
        break;
    case 7: // verb
        if (target.size() != 7) {
            fail("invalid verb suffix target in ADDONS.LAT");
        }
        result.paradigm = paradigm(3, 4);
        result.attribute_0 = enum_value(target[5], {"X"}, "verb kind");
        result.target_key = parse_u8(target[6], "suffix target key");
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
    case 1: // noun
        if (target.size() != 5U) {
            fail("invalid noun tackon target in ADDONS.LAT");
        }
        result.paradigm = paradigm();
        result.attribute_0 =
            enum_value(target[3], {"X", "M", "F", "N", "C"}, "gender");
        result.attribute_1 = enum_value(
            target[4], {"x", "s", "m", "a", "g", "n", "p", "t", "l", "w"},
            "noun kind");
        break;
    case 2: // pronoun
    case 3: // pack
        if (target.size() != 4U) {
            fail("invalid pronoun tackon target in ADDONS.LAT");
        }
        result.paradigm = paradigm();
        result.attribute_0 = enum_value(target[3],
                                        {"X", "PERS", "REL", "REFLEX", "DEMONS",
                                         "INTERR", "INDEF", "ADJECT"},
                                        "pronoun kind");
        break;
    case 4: // adjective
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
    result.packon = result.base == 3U && (which == 1U || which == 2U) &&
                    result.meaning.starts_with("PACKON w/");
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
        if (meaning_line.size() > 80U) {
            meaning_line.resize(80U);
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
    if (surface.empty() || surface.size() > 18U) {
        fail("unique surface is empty or exceeds the legacy stem width");
    }
    if (meaning.size() > 80U) {
        // Meaning_Type is fixed-width in Ada, so retaining the truncation is
        // required for byte-for-byte canonical JSON compatibility.
        meaning.resize(80U);
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
        const auto gender = enum_value(fields.at(5), {"X", "M", "F", "N", "C"},
                                       "unique gender", source);
        return static_cast<std::uint16_t>(
            static_cast<std::uint16_t>(grammatical_case) |
            (static_cast<std::uint16_t>(number) << 3U) |
            (static_cast<std::uint16_t>(gender) << 5U));
    };

    std::size_t translation_offset = 0;
    switch (result.part_of_speech) {
    case 1: // noun
        if (fields.size() != 12U) {
            fail("invalid noun record shape in UNIQUES.LAT");
        }
        result.paradigm = paradigm(1, 2);
        result.morphology = nominal_morphology();
        static_cast<void>(enum_value(
            fields[6], {"X", "S", "M", "A", "G", "N", "P", "T", "L", "W"},
            "unique noun kind", source));
        translation_offset = 7U;
        break;
    case 2: // pronoun
        if (fields.size() != 12U) {
            fail("invalid pronoun record shape in UNIQUES.LAT");
        }
        result.paradigm = paradigm(1, 2);
        result.morphology = nominal_morphology();
        static_cast<void>(enum_value(fields[6],
                                     {"X", "PERS", "REL", "REFLEX", "DEMONS",
                                      "INTERR", "INDEF", "ADJECT"},
                                     "unique pronoun kind", source));
        translation_offset = 7U;
        break;
    case 4: { // adjective
        if (fields.size() != 12U) {
            fail("invalid adjective record shape in UNIQUES.LAT");
        }
        result.paradigm = paradigm(1, 2);
        const auto degree = enum_value(fields[6], {"X", "POS", "COMP", "SUPER"},
                                       "unique degree", source);
        result.morphology = static_cast<std::uint16_t>(
            nominal_morphology() | (static_cast<std::uint16_t>(degree) << 8U));
        translation_offset = 7U;
        break;
    }
    case 7: { // verb
        if (fields.size() != 14U) {
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
        const auto person = parse_u8(fields[6], "unique person", source);
        const auto number =
            enum_value(fields[7], {"X", "S", "P"}, "unique number", source);
        if (person > 3U) {
            fail("unique verb person is outside 0..3");
        }
        static_cast<void>(
            enum_value(fields[8],
                       {"X", "TO_BE", "TO_BEING", "GEN", "DAT", "ABL", "TRANS",
                        "INTRANS", "IMPERS", "DEP", "SEMIDEP", "PERFDEF"},
                       "unique verb kind", source));
        result.morphology = static_cast<std::uint16_t>(
            static_cast<std::uint16_t>(tense) |
            (static_cast<std::uint16_t>(voice) << 3U) |
            (static_cast<std::uint16_t>(mood) << 5U) |
            (static_cast<std::uint16_t>(person) << 8U) |
            (static_cast<std::uint16_t>(number) << 10U));
        translation_offset = 9U;
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
    result.translation = static_cast<std::uint32_t>(age) |
                         (static_cast<std::uint32_t>(area) << 4U) |
                         (static_cast<std::uint32_t>(geography) << 8U) |
                         (static_cast<std::uint32_t>(frequency) << 13U) |
                         (static_cast<std::uint32_t>(dictionary_source) << 17U);
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
        if (fields.size() != 15U) {
            fail("invalid REWRITES.LAT record shape");
        }
        if (meaning.size() > 80U) {
            meaning.resize(80U);
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
        rewrite.scan_reverse = enum_value(fields[5], {"FORWARD", "REVERSE"},
                                          "rewrite direction", source) == 1U;
        rewrite.before = fields[6] == "-" ? "" : std::string{fields[6]};
        rewrite.after = fields[7] == "-" ? "" : std::string{fields[7]};
        rewrite.required_part = part_of_speech(fields[8], source);
        rewrite.required_stem_key =
            parse_u8(fields[9], "required stem key", source);
        rewrite.minimum_before =
            parse_u8(fields[10], "minimum prefix size", source);
        rewrite.minimum_after =
            parse_u8(fields[11], "minimum suffix size", source);
        rewrite.constraint =
            enum_value(fields[12], {"ANY", "EO_VERB", "ADJECTIVE_IIS"},
                       "rewrite constraint", source);
        rewrite.medieval = enum_value(fields[13], {"CLASSICAL", "MEDIEVAL"},
                                      "rewrite era", source) == 1U;
        rewrite.name = fields[14];
        rewrite.meaning = std::move(meaning);
        if (rewrite.kind == 0U || rewrite.scope == 0U ||
            rewrite.operation == 0U || rewrite.stage == 0U ||
            rewrite.required_stem_key > 4U || rewrite.name.empty() ||
            (rewrite.before.empty() && rewrite.operation != 3U) ||
            rewrite.minimum_before > 15U || rewrite.minimum_after > 15U) {
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
        if (const auto comment = line.find("--");
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
        if (fields.front() == "STEM" && fields.size() == 5U) {
            const auto entry = parse_u32(fields[1], "dictionary entry", source);
            if (entry == 0U ||
                entry > std::numeric_limits<std::uint16_t>::max()) {
                fail("dictionary entry exceeds one-based u16 in QUANTITIES.LAT");
            }
            const auto slot = parse_u8(fields[2], "lexical slot", source);
            if (slot < 1U || slot > 4U) {
                fail("lexical slot is outside 1..4 in QUANTITIES.LAT");
            }
            result.stems.push_back({
                static_cast<std::uint16_t>(entry), slot,
                parse_u32(fields[3], "known mask", source),
                parse_u32(fields[4], "long mask", source),
            });
            continue;
        }
        fail("invalid QUANTITIES.LAT record shape: " + line);
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
    return pofs == 1 || pofs == 2 || pofs == 3 || pofs == 4 || pofs == 5 ||
           pofs == 7 || pofs == 8 || pofs == 9;
}

std::uint8_t pack_paradigm(std::uint32_t which, std::uint32_t variant) {
    if (which > 9 || variant > 9) {
        fail("paradigm component outside 0..9");
    }
    return static_cast<std::uint8_t>((which << 4U) | variant);
}

std::uint32_t pack_translation(std::span<const std::byte> record) {
    const auto age = byte_at(record, 92);
    const auto area = byte_at(record, 93);
    const auto geo = byte_at(record, 94);
    const auto frequency = byte_at(record, 95);
    const auto source = byte_at(record, 96);

    if (age > 8 || area > 11 || geo > 17 || frequency > 9 || source > 25) {
        fail("dictionary metadata enum outside legacy range");
    }

    return static_cast<std::uint32_t>(age) |
           (static_cast<std::uint32_t>(area) << 4U) |
           (static_cast<std::uint32_t>(geo) << 8U) |
           (static_cast<std::uint32_t>(frequency) << 13U) |
           (static_cast<std::uint32_t>(source) << 17U);
}

std::uint16_t pack_inflection_morphology(std::span<const std::byte> record,
                                         std::uint8_t pofs) {
    auto at = [&](std::size_t offset) { return byte_at(record, offset); };

    switch (pofs) {
    case 1: // noun
    case 2: // pronoun
    case 3: // pack
    case 9: // supine
        return static_cast<std::uint16_t>(at(12) | (at(13) << 3U) |
                                          (at(14) << 5U));
    case 4: // adjective
    case 5: // numeral
        return static_cast<std::uint16_t>(at(12) | (at(13) << 3U) |
                                          (at(14) << 5U) | (at(15) << 8U));
    case 6:  // adverb
    case 10: // preposition
        return at(4);
    case 7: // finite verb
        return static_cast<std::uint16_t>(at(12) | (at(13) << 3U) |
                                          (at(14) << 5U) | (at(15) << 8U) |
                                          (at(16) << 10U));
    case 8: // participle
        return static_cast<std::uint16_t>(at(12) | (at(13) << 3U) |
                                          (at(14) << 5U) | (at(15) << 8U) |
                                          (at(16) << 11U) | (at(17) << 13U));
    default:
        return 0;
    }
}

std::size_t stem_bucket(std::string_view stem) {
    if (stem.empty()) {
        return 0;
    }
    const auto lower_ascii = [](char ch) {
        if (ch >= 'A' && ch <= 'Z') {
            ch = static_cast<char>(ch - 'A' + 'a');
        }
        // The legacy stem index folds the Latin orthographic pairs i/j and
        // u/v before choosing its one- or two-letter bucket.
        if (ch == 'j') {
            return 'i';
        }
        if (ch == 'v') {
            return 'u';
        }
        return ch;
    };
    const auto first_character = lower_ascii(stem.front());
    if (first_character < 'a' || first_character > 'z') {
        fail("stem outside lowercase a-z index: " + std::string(stem));
    }

    const auto first = static_cast<std::size_t>(first_character - 'a');
    const auto base = 1 + first * 27;
    if (stem.size() == 1) {
        return base;
    }
    const auto second_character = lower_ascii(stem[1]);
    if (second_character < 'a' || second_character > 'z') {
        fail("second stem character outside lowercase a-z index: " +
             std::string(stem));
    }
    return base + 1 + static_cast<std::size_t>(second_character - 'a');
}

std::uint32_t crc32(std::span<const std::byte> bytes, std::uint32_t crc = 0) {
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

Bytes byte_shuffle(Bytes rows, std::size_t count, std::size_t stride) {
    if (rows.size() != count * stride) {
        fail("cannot byte-shuffle section with inconsistent shape");
    }
    Bytes columns;
    columns.reserve(rows.size());
    for (std::size_t byte_column = 0; byte_column < stride; ++byte_column) {
        for (std::size_t row = 0; row < count; ++row) {
            columns.push_back(rows[row * stride + byte_column]);
        }
    }
    return columns;
}

Bytes make_image(std::vector<Section> sections, PackingProfile profile) {
    constexpr std::uint32_t fixed_header_size = 40;
    constexpr std::uint32_t directory_entry_size = 32;

    const auto section_count = static_cast<std::uint32_t>(sections.size());
    const auto header_bytes =
        fixed_header_size + section_count * directory_entry_size;

    std::uint64_t file_size = header_bytes;
    std::uint32_t payload_crc = 0;
    for (const auto &section : sections) {
        file_size += section.data.size();
        payload_crc = crc32(section.data, payload_crc);
    }

    Bytes output;
    output.reserve(static_cast<std::size_t>(file_size));
    for (const char ch : std::string_view{"WWDB\r\n\x1a\n", 8}) {
        append_u8(output, static_cast<std::uint8_t>(ch));
    }
    append_u16_le(output, 1); // major
    append_u16_le(output, 8); // minor: typed packon selection metadata
    append_u32_le(output, fixed_header_size);
    append_u32_le(output, section_count);
    append_u32_le(output, std::to_underlying(profile));
    append_u64_le(output, file_size);
    append_u32_le(output, payload_crc);
    append_u32_le(output, 0); // reserved

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
    if (argc != 3 && argc != 4) {
        std::cerr << "usage: wwdb_poc_pack REPOSITORY_ROOT OUTPUT.wwdb "
                     "[simple|dense|columnar|search-only]\n";
        return 2;
    }

    const std::filesystem::path root = argv[1];
    const std::filesystem::path output_path = argv[2];
    PackingProfile profile = PackingProfile::simple;
    if (argc == 4) {
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

    const auto dictionary = read_file(root / "DICTFILE.GEN");
    const auto stems = read_file(root / "STEMFILE.GEN");
    const auto inflections = read_file(root / "INFLECTS.SEC");
    const auto addons = read_addons(root / "ADDONS.LAT");
    const auto uniques = read_uniques(root / "UNIQUES.LAT");
    const auto rewrites = read_rewrites(root / "REWRITES.LAT");
    const auto quantities = read_quantities(root / "QUANTITIES.LAT");

    if (dictionary.size() % dictionary_record_size != 0 ||
        stems.size() % stem_record_size != 0 ||
        inflections.size() != inflection_record_size * inflections_per_section *
                                  inflection_section_count) {
        fail("unexpected legacy file size");
    }

    const auto lexeme_count = dictionary.size() / dictionary_record_size;
    const auto stem_reference_count = stems.size() / stem_record_size;
    if (lexeme_count + uniques.size() >
            std::numeric_limits<std::uint16_t>::max() + 1ULL ||
        stem_reference_count > std::numeric_limits<std::uint16_t>::max()) {
        fail("PoC u16 record/reference capacity exceeded");
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
        !use_dense_records ? 19 : (include_meanings ? 16 : 14);
    lexeme_records.reserve(lexeme_count * lexeme_stride);

    for (std::size_t index = 0; index < lexeme_count; ++index) {
        const auto record = std::span{dictionary}.subspan(
            index * dictionary_record_size, dictionary_record_size);

        std::array<std::uint16_t, 4> stem_ids{};
        for (std::size_t stem_index = 0; stem_index < stem_ids.size();
             ++stem_index) {
            stem_ids[stem_index] =
                stem_pool.intern(fixed_string(record, stem_index * 18, 18));
        }
        const auto meaning = fixed_string(record, 97, 80);
        std::uint16_t meaning_id = 0;
        if (include_meanings) {
            meaning_id = meaning_pool.intern(meaning);
        }

        const auto pofs = byte_at(record, 72);
        if (pofs > 15) {
            fail("dictionary part-of-speech outside legacy range");
        }
        const auto which = has_paradigm(pofs) ? read_u32_le(record, 76) : 0;
        const auto variant = has_paradigm(pofs) ? read_u32_le(record, 80) : 0;
        const auto paradigm = pack_paradigm(which, variant);

        std::uint8_t attribute_0 = 0;
        std::uint8_t attribute_1 = 0;
        std::uint16_t numeric_value = 0;
        switch (pofs) {
        case 1:                                // noun
            attribute_0 = byte_at(record, 84); // gender
            attribute_1 = byte_at(record, 85); // noun kind
            break;
        case 2: // pronoun
        case 3: // pack
        case 4: // adjective
        case 7: // verb
            attribute_0 = byte_at(record, 84);
            break;
        case 5: // numeral
            attribute_0 = byte_at(record, 84);
            numeric_value = static_cast<std::uint16_t>(read_u32_le(record, 88));
            break;
        case 6:  // adverb
        case 10: // preposition
            attribute_0 = byte_at(record, 76);
            break;
        default:
            break;
        }
        const auto translation = pack_translation(record);
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
            case 1: // noun: gender:3 | noun kind:4
                if (attribute_0 > 7 || attribute_1 > 15) {
                    fail("noun attribute outside dense profile");
                }
                class_payload = static_cast<std::uint64_t>(attribute_0) |
                                (static_cast<std::uint64_t>(attribute_1) << 3U);
                break;
            case 2: // pronoun kind
            case 4: // adjective comparison
            case 7: // verb kind
                if (attribute_0 > 15) {
                    fail("lexeme attribute outside dense profile");
                }
                class_payload = attribute_0;
                break;
            case 3: { // pack kind + required packon addon
                if (attribute_0 > 15) {
                    fail("pack attribute outside dense profile");
                }
                std::uint16_t packon_plus_one = 0;
                for (const auto &tackon : addons.tackons) {
                    if (!tackon.packon) {
                        continue;
                    }
                    // The closing delimiter is semantically significant: a
                    // prefix comparison would mistake -cum for -cumque.
                    const auto marker =
                        std::string{"(w/-"} + tackon.fix + ')';
                    if (!meaning.starts_with(marker)) {
                        continue;
                    }
                    if (packon_plus_one != 0U || tackon.addon_id >= 511U) {
                        fail("pack lexeme has ambiguous/out-of-range packon");
                    }
                    packon_plus_one =
                        static_cast<std::uint16_t>(tackon.addon_id + 1U);
                }
                class_payload =
                    static_cast<std::uint64_t>(attribute_0) |
                    (static_cast<std::uint64_t>(packon_plus_one) << 4U);
                break;
            }
            case 5: // numeral sort:3 | numeric value:10
                if (attribute_0 > 7 || numeric_value > 1023) {
                    fail("numeral payload outside dense profile");
                }
                class_payload =
                    static_cast<std::uint64_t>(attribute_0) |
                    (static_cast<std::uint64_t>(numeric_value) << 3U);
                break;
            case 6:  // adverb comparison
            case 10: // preposition case
                if (attribute_0 > 15) {
                    fail("lexeme attribute outside dense profile");
                }
                class_payload = attribute_0;
                break;
            default:
                break;
            }

            const std::uint64_t metadata =
                static_cast<std::uint64_t>(pofs) |
                (static_cast<std::uint64_t>(paradigm) << 4U) |
                (static_cast<std::uint64_t>(translation) << 12U) |
                (class_payload << 34U);
            append_u48_le(lexeme_records, metadata);
        }
    }

    Bytes stem_reference_records;
    const std::size_t stem_reference_stride = use_dense_records ? 3 : 5;
    stem_reference_records.reserve(stem_reference_count *
                                   stem_reference_stride);
    std::array<std::uint16_t, 703> stem_bucket_counts{};
    std::size_t previous_bucket = 0;
    std::string previous_stem;

    for (std::size_t index = 0; index < stem_reference_count; ++index) {
        const auto record = std::span{stems}.subspan(index * stem_record_size,
                                                     stem_record_size);
        const auto stem = fixed_string(record, 0, 18);
        const auto bucket = stem_bucket(stem);
        if (index != 0 && bucket < previous_bucket) {
            fail("STEMFILE is not monotonic in the proposed prefix order at " +
                 std::to_string(index) + ": [" + previous_stem + "] bucket " +
                 std::to_string(previous_bucket) + " -> [" + stem +
                 "] bucket " + std::to_string(bucket));
        }
        previous_bucket = bucket;
        previous_stem = stem;
        ++stem_bucket_counts.at(bucket);

        const auto mnpc = read_u64_le(record, 48);
        if (mnpc == 0 || mnpc > lexeme_count) {
            fail("STEMFILE MNPC outside dictionary range");
        }
        const auto key = read_u32_le(record, 40);
        if (key > 4) {
            fail("STEMFILE key outside observed 0..4 range");
        }

        const auto lexeme_id = static_cast<std::uint16_t>(mnpc - 1);
        if (!use_dense_records) {
            append_u16_le(stem_reference_records, stem_pool.id_of(stem));
            append_u16_le(stem_reference_records, lexeme_id);
            append_u8(stem_reference_records, static_cast<std::uint8_t>(key));
        } else {
            const auto dictionary_record = std::span{dictionary}.subspan(
                static_cast<std::size_t>(lexeme_id) * dictionary_record_size,
                dictionary_record_size);
            std::size_t lexical_slot = 4;
            if (key >= 1 && key <= 4 &&
                fixed_string(dictionary_record, (key - 1) * 18, 18) == stem) {
                lexical_slot = key - 1;
            } else {
                for (std::size_t candidate = 0; candidate < 4; ++candidate) {
                    if (fixed_string(dictionary_record, candidate * 18, 18) ==
                        stem) {
                        lexical_slot = candidate;
                        break;
                    }
                }
            }
            if (lexical_slot == 4) {
                fail("STEMFILE stem is absent from referenced lexeme: " + stem);
            }

            const auto packed_reference =
                static_cast<std::uint32_t>(lexeme_id) |
                (static_cast<std::uint32_t>(lexical_slot) << 16U) |
                (key << 18U);
            append_u24_le(stem_reference_records, packed_reference);
        }
    }

    Bytes stem_boundaries;
    std::uint32_t stem_boundary = 0;
    append_u16_le(stem_boundaries, 0);
    for (const auto count : stem_bucket_counts) {
        stem_boundary += count;
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
            const auto pofs = byte_at(record, 0);
            if (pofs == 0) {
                continue;
            }
            if (pofs > 15) {
                fail("inflection part-of-speech outside legacy range");
            }

            const auto which = has_paradigm(pofs) ? read_u32_le(record, 4) : 0;
            const auto variant =
                has_paradigm(pofs) ? read_u32_le(record, 8) : 0;
            const auto stem_key = read_u32_le(record, 20);
            const auto ending_size = read_u32_le(record, 24);
            if (stem_key < 1 || stem_key > 4 || ending_size > 7) {
                fail("inflection key/ending size outside compact range");
            }

            const auto ending = fixed_string(record, 28, ending_size);
            const auto ending_id = ending_pool.intern(ending);
            if (ending_id > 0x1ffU) {
                fail("PoC 9-bit ending ID capacity exceeded");
            }

            const auto age = byte_at(record, 36);
            const auto frequency = byte_at(record, 37);
            if (age > 8 || frequency > 9) {
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
                    (static_cast<std::uint64_t>(paradigm) << 4U) |
                    (static_cast<std::uint64_t>(morphology) << 12U) |
                    (static_cast<std::uint64_t>(ending_id) << 28U) |
                    (static_cast<std::uint64_t>(stem_key - 1U) << 37U) |
                    (static_cast<std::uint64_t>(age) << 39U) |
                    (static_cast<std::uint64_t>(frequency) << 43U);
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
        if (quantity.rule_id >= dense_inflection_count || quantity.known == 0U ||
            quantity.known > 0x7fU || quantity.long_vowel > 0x7fU ||
            (quantity.long_vowel & ~quantity.known) != 0U) {
            fail("invalid inflection quantity in QUANTITIES.LAT");
        }
        const auto &ending = inflection_endings.at(quantity.rule_id);
        const auto ending_size = ending.size();
        const auto valid_bits = ending_size == 0U
                                    ? 0U
                                    : (std::uint32_t{1U} << ending_size) - 1U;
        if (((quantity.known | quantity.long_vowel) & ~valid_bits) != 0U ||
            !quantity_positions_are_vowels(ending, quantity.known) ||
            packed_inflection_quantities[quantity.rule_id] != 0U) {
            fail("duplicate or out-of-range inflection quantity");
        }
        packed_inflection_quantities[quantity.rule_id] =
            static_cast<std::uint16_t>(quantity.known |
                                       (quantity.long_vowel << 7U));
    }
    Bytes inflection_quantity_records;
    inflection_quantity_records.reserve(
        packed_inflection_quantities.size() * 2U);
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
            fixed_string(record, lexical_slot * 18U, 18U);
        const auto valid_bits = stem.empty()
                                    ? 0U
                                    : (std::uint32_t{1U} << stem.size()) - 1U;
        if (stem.empty() || quantity.known == 0U ||
            quantity.known > 0x03ffffU ||
            quantity.long_vowel > 0x03ffffU ||
            (quantity.long_vowel & ~quantity.known) != 0U ||
            ((quantity.known | quantity.long_vowel) & ~valid_bits) != 0U ||
            !quantity_positions_are_vowels(stem, quantity.known)) {
            fail("invalid stem quantity in QUANTITIES.LAT");
        }
        packed_stem_quantities.push_back({
            static_cast<std::uint32_t>(lexeme_id) |
                (static_cast<std::uint32_t>(lexical_slot) << 16U),
            quantity.known,
            quantity.long_vowel,
        });
    }
    std::ranges::sort(packed_stem_quantities, {},
                      &PackedStemQuantity::key);
    if (std::ranges::adjacent_find(
            packed_stem_quantities, std::ranges::equal_to{},
            &PackedStemQuantity::key) != packed_stem_quantities.end()) {
        fail("duplicate stem quantity in QUANTITIES.LAT");
    }
    Bytes stem_quantity_records;
    stem_quantity_records.reserve(packed_stem_quantities.size() * 9U);
    for (const auto &quantity : packed_stem_quantities) {
        append_u24_le(stem_quantity_records, quantity.key);
        append_u24_le(stem_quantity_records, quantity.known);
        append_u24_le(stem_quantity_records, quantity.long_vowel);
    }

    Bytes suffix_records;
    const std::uint32_t suffix_stride = include_meanings ? 14U : 12U;
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
            (static_cast<std::uint64_t>(suffix.root_key) << 4U) |
            (static_cast<std::uint64_t>(suffix.target) << 7U) |
            (static_cast<std::uint64_t>(suffix.target_key) << 11U) |
            (static_cast<std::uint64_t>(suffix.paradigm) << 14U) |
            (static_cast<std::uint64_t>(suffix.attribute_0) << 22U) |
            (static_cast<std::uint64_t>(suffix.attribute_1) << 26U) |
            (static_cast<std::uint64_t>(suffix.numeric_value) << 30U) |
            (static_cast<std::uint64_t>(suffix.connect) << 38U);
        append_u64_le(suffix_records, metadata);
    }

    Bytes prefix_records;
    const std::uint32_t prefix_stride = include_meanings ? 8U : 6U;
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
            (static_cast<std::uint16_t>(prefix.target) << 4U) |
            (static_cast<std::uint16_t>(prefix.connect) << 8U));
        append_u16_le(prefix_records, metadata);
    }

    Bytes tackon_records;
    const std::uint32_t tackon_stride = include_meanings ? 10U : 8U;
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
            (static_cast<std::uint32_t>(tackon.paradigm) << 4U) |
            (static_cast<std::uint32_t>(tackon.attribute_0) << 12U) |
            (static_cast<std::uint32_t>(tackon.attribute_1) << 16U) |
            (static_cast<std::uint32_t>(tackon.packon) << 20U) |
            (static_cast<std::uint32_t>(tackon.enclitic) << 21U);
        append_u32_le(tackon_records, metadata);
    }

    Bytes unique_records;
    const std::uint32_t unique_stride = include_meanings ? 12U : 10U;
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
            (static_cast<std::uint64_t>(unique.paradigm) << 4U) |
            (static_cast<std::uint64_t>(unique.morphology) << 12U) |
            (static_cast<std::uint64_t>(unique.translation) << 28U);
        append_u64_le(unique_records, metadata);
    }

    Bytes rewrite_records;
    const std::uint32_t rewrite_stride = include_meanings ? 16U : 14U;
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
            (static_cast<std::uint32_t>(rewrite.scope) << 2U) |
            (static_cast<std::uint32_t>(rewrite.priority) << 4U) |
            (static_cast<std::uint32_t>(rewrite.scan_reverse) << 12U) |
            (static_cast<std::uint32_t>(rewrite.required_part) << 13U) |
            (static_cast<std::uint32_t>(rewrite.required_stem_key) << 17U) |
            (static_cast<std::uint32_t>(rewrite.minimum_before) << 20U) |
            (static_cast<std::uint32_t>(rewrite.minimum_after) << 24U) |
            (static_cast<std::uint32_t>(rewrite.medieval) << 28U);
        append_u32_le(rewrite_records, metadata);
        // Integer promotions apply to each shift and OR.  Narrow only after
        // composing the validated seven-bit payload so conversion warnings do
        // not hide an accidental future expansion of the wire field.
        const auto behavior = static_cast<std::uint16_t>(
            static_cast<std::uint16_t>(rewrite.operation) |
            (static_cast<std::uint16_t>(rewrite.stage) << 3U) |
            (static_cast<std::uint16_t>(rewrite.constraint) << 5U));
        append_u16_le(rewrite_records, behavior);
    }

    std::vector<Section> sections;
    sections.push_back({SectionType::stem_strings, 0, stem_pool.size(), 0,
                        stem_pool.encode()});
    if (include_meanings) {
        sections.push_back({SectionType::meaning_strings, 0,
                            meaning_pool.size(), 0, meaning_pool.encode()});
    }
    sections.push_back({SectionType::ending_strings, 0, ending_pool.size(), 0,
                        ending_pool.encode()});
    if (use_byte_columns) {
        lexeme_records = byte_shuffle(std::move(lexeme_records), lexeme_count,
                                      lexeme_stride);
        stem_reference_records =
            byte_shuffle(std::move(stem_reference_records),
                         stem_reference_count, stem_reference_stride);
        inflection_records = byte_shuffle(std::move(inflection_records),
                                          dense_inflection_count, 6);
    }

    const std::uint32_t record_flags = use_byte_columns ? 2U : 1U;
    sections.push_back({SectionType::lexemes, record_flags,
                        static_cast<std::uint32_t>(lexeme_count),
                        static_cast<std::uint32_t>(lexeme_stride),
                        std::move(lexeme_records)});
    sections.push_back({SectionType::stem_references, record_flags,
                        static_cast<std::uint32_t>(stem_reference_count),
                        static_cast<std::uint32_t>(stem_reference_stride),
                        std::move(stem_reference_records)});
    sections.push_back({SectionType::stem_prefix_boundaries, 1, 704, 2,
                        std::move(stem_boundaries)});
    sections.push_back({SectionType::inflections, record_flags,
                        dense_inflection_count, use_dense_records ? 6U : 8U,
                        std::move(inflection_records)});
    sections.push_back({SectionType::inflection_section_boundaries, 1, 6, 2,
                        std::move(inflection_boundaries)});
    sections.push_back({SectionType::inflection_quantities, 1,
                        dense_inflection_count, 2,
                        std::move(inflection_quantity_records)});
    sections.push_back({
        SectionType::stem_quantities,
        1,
        static_cast<std::uint32_t>(packed_stem_quantities.size()),
        9,
        std::move(stem_quantity_records),
    });
    sections.push_back({SectionType::suffix_strings, 0,
                        suffix_string_pool.size(), 0,
                        suffix_string_pool.encode()});
    if (include_meanings) {
        sections.push_back({SectionType::suffix_meanings, 0,
                            suffix_meaning_pool.size(), 0,
                            suffix_meaning_pool.encode()});
    }
    sections.push_back({SectionType::suffixes, 1,
                        static_cast<std::uint32_t>(addons.suffixes.size()),
                        suffix_stride, std::move(suffix_records)});
    sections.push_back({SectionType::prefix_strings, 0,
                        prefix_string_pool.size(), 0,
                        prefix_string_pool.encode()});
    if (include_meanings) {
        sections.push_back({SectionType::prefix_meanings, 0,
                            prefix_meaning_pool.size(), 0,
                            prefix_meaning_pool.encode()});
    }
    sections.push_back({SectionType::prefixes, 1,
                        static_cast<std::uint32_t>(addons.prefixes.size()),
                        prefix_stride, std::move(prefix_records)});
    sections.push_back({SectionType::tackon_strings, 0,
                        tackon_string_pool.size(), 0,
                        tackon_string_pool.encode()});
    if (include_meanings) {
        sections.push_back({SectionType::tackon_meanings, 0,
                            tackon_meaning_pool.size(), 0,
                            tackon_meaning_pool.encode()});
    }
    sections.push_back({SectionType::tackons, 1,
                        static_cast<std::uint32_t>(addons.tackons.size()),
                        tackon_stride, std::move(tackon_records)});
    sections.push_back({SectionType::uniques, 1,
                        static_cast<std::uint32_t>(uniques.size()),
                        unique_stride, std::move(unique_records)});
    sections.push_back({SectionType::rewrite_strings, 0,
                        rewrite_string_pool.size(), 0,
                        rewrite_string_pool.encode()});
    if (include_meanings) {
        sections.push_back({SectionType::rewrite_meanings, 0,
                            rewrite_meaning_pool.size(), 0,
                            rewrite_meaning_pool.encode()});
    }
    sections.push_back({SectionType::rewrites, 1,
                        static_cast<std::uint32_t>(rewrites.size()),
                        rewrite_stride, std::move(rewrite_records)});

    const auto image = make_image(std::move(sections), profile);
    std::filesystem::create_directories(output_path.parent_path());
    write_file(output_path, image);

    std::cout << "wrote " << output_path << "\n"
              << "bytes=" << image.size() << "\n"
              << "profile=" << std::to_underlying(profile) << "\n"
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
