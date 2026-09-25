#pragma once

#include "words/database.hpp"
#include "words/model.hpp"

#include <array>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace words {

enum class QuantityCoverage : std::uint8_t {
    none,
    partial,
    complete,
};

enum class QuantityOrigin : std::uint8_t {
    stem,
    suffix,
    ending,
};

enum class QuantityDisplayMode : std::uint8_t {
    database_only,
    legacy_database_and_input,
};

struct ResolvedQuantityPosition final {
    // Zero-based logical Latin-letter index. Combining quantity marks do not
    // occupy an index of their own.
    std::uint32_t index{};
    VowelQuantity quantity{VowelQuantity::unknown};
    QuantityOrigin origin{QuantityOrigin::stem};
    auto operator<=>(const ResolvedQuantityPosition &) const = default;
};

struct ResolvedQuantity final {
    // Contains database evidence only; never includes user-supplied marks.
    std::optional<std::string> annotated;
    QuantityCoverage coverage{QuantityCoverage::none};
    std::vector<ResolvedQuantityPosition> positions;
    auto operator<=>(const ResolvedQuantity &) const = default;
};

struct ResolvedForm final {
    std::string stem;
    std::uint8_t stem_key{};
    std::string ending;
    std::string recognized;
    // Presentation-ready NFC spelling using database evidence only. The
    // recognized spelling retains explicit input quantities separately.
    // Published native v3 explicitly requests its legacy combined display.
    std::string display;
    ResolvedQuantity quantity;
    auto operator<=>(const ResolvedForm &) const = default;
};

[[nodiscard]] ResolvedForm resolved_form(const Database &database,
                                         const SurfaceForm &surface,
                                         const AnalysisIR &analysis,
                                         bool include_suffix_quantity = true,
                                         QuantityDisplayMode display_mode =
                                             QuantityDisplayMode::database_only);

[[nodiscard]] ResolvedForm unquantified_form(std::string stem,
                                             std::uint8_t stem_key,
                                             std::string ending,
                                             std::string recognized,
                                             QuantityDisplayMode display_mode =
                                                 QuantityDisplayMode::database_only);

// Removes user-supplied quantity marks without claiming database evidence.
[[nodiscard]] std::string
display_without_input_quantity(std::string_view recognized);

[[nodiscard]] constexpr std::string_view
quantity_coverage_name(const QuantityCoverage value) noexcept {
    if (value == QuantityCoverage::partial) {
        return "partial";
    }
    if (value == QuantityCoverage::complete) {
        return "complete";
    }
    return "none";
}

[[nodiscard]] constexpr std::string_view
quantity_origin_name(const QuantityOrigin value) noexcept {
    if (value == QuantityOrigin::suffix) {
        return "suffix";
    }
    if (value == QuantityOrigin::ending) {
        return "ending";
    }
    return "stem";
}

// Stable semantic ordering for presentation backends. The key deliberately
// contains no serialized JSON, so formatting or schema changes cannot reorder
// otherwise identical engine results.
enum class AnalysisDictionaryOrder : std::uint8_t {
    general,
    roman_numeral,
    unique,
};

enum class DerivationOrder : std::uint8_t {
    compound,
    derived,
    orthographic,
    regular,
    syncope,
    unique,
};

inline constexpr std::size_t morphology_order_field_count{8U};
using MorphologyOrderKey =
    std::array<std::string, morphology_order_field_count>;

struct AnalysisOrderKey final {
    AnalysisDictionaryOrder dictionary{AnalysisDictionaryOrder::general};
    std::uint32_t dictionary_entry{};
    std::string part_of_speech;
    std::uint8_t stem_key{};
    std::string stem;
    std::string ending;
    MorphologyOrderKey morphology;
    DerivationOrder derivation_order{DerivationOrder::regular};
    DerivationIR source_derivation;
    DerivationIR auxiliary_derivation;
    CompoundKind compound_kind{CompoundKind::unknown};
    std::string auxiliary;
    std::uint32_t artificial_value{};
    bool artificial_malformed{};

    auto operator<=>(const AnalysisOrderKey &) const = default;
};

[[nodiscard]] AnalysisOrderKey analysis_order_key(const Database &database,
                                                  const SurfaceForm &surface,
                                                  const AnalysisIR &analysis);
[[nodiscard]] AnalysisOrderKey
analysis_order_key(const Database &database,
                   const CompoundAnalysisIR &analysis);
[[nodiscard]] AnalysisOrderKey
analysis_order_key(const RomanNumeralIR &analysis);

} // namespace words
