#pragma once

#include "words/database.hpp"
#include "words/model.hpp"

#include <array>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <string>

namespace words {

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
