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

struct ResolvedQuantityPosition final {
    // Zero-based logical Latin-letter index. Combining quantity marks do not
    // occupy an index of their own.
    std::uint32_t index{};
    VowelQuantity quantity{VowelQuantity::unknown};
    QuantityOrigin origin{QuantityOrigin::stem};
    auto operator<=>(const ResolvedQuantityPosition &) const = default;
};

struct ResolvedQuantity final {
    // Contains database evidence only. User-supplied marks are represented in
    // ResolvedForm::display, and never masquerade as lexical evidence here.
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
    // Presentation-ready NFC spelling. Database evidence wins where known;
    // explicit input quantities survive at positions absent from the DB.
    std::string display;
    ResolvedQuantity quantity;
    auto operator<=>(const ResolvedForm &) const = default;
};

[[nodiscard]] ResolvedForm resolved_form(const Database &database,
                                         const SurfaceForm &surface,
                                         const AnalysisIR &analysis,
                                         bool include_suffix_quantity = true);

[[nodiscard]] ResolvedForm unquantified_form(std::string stem,
                                             std::uint8_t stem_key,
                                             std::string ending,
                                             std::string recognized);

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
