#include "markov_features.hpp"
#include "markov_model.hpp"
#include "parser.hpp"

#include "words/engine.hpp"
#include "words/semantics.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <expected>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <map>
#include <optional>
#include <random>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#ifndef PARSERS_INVESTIGATION_WWDB_PATH
#error "PARSERS_INVESTIGATION_WWDB_PATH must point to a WWDB image"
#endif

#ifndef PARSERS_INVESTIGATION_CORPUS_PATH
#error "PARSERS_INVESTIGATION_CORPUS_PATH must point to the fixture corpus"
#endif

#ifndef PARSERS_INVESTIGATION_DATASET_ID
#error "PARSERS_INVESTIGATION_DATASET_ID must identify the WWDB dataset"
#endif

#ifndef PARSERS_INVESTIGATION_GIT_COMMIT
#define PARSERS_INVESTIGATION_GIT_COMMIT "unknown"
#endif

#ifndef PARSERS_INVESTIGATION_COMPILER
#define PARSERS_INVESTIGATION_COMPILER "unknown"
#endif

#ifndef PARSERS_INVESTIGATION_COMPILER_VERSION
#define PARSERS_INVESTIGATION_COMPILER_VERSION "unknown"
#endif

#ifndef PARSERS_INVESTIGATION_BUILD_TYPE
#define PARSERS_INVESTIGATION_BUILD_TYPE "unknown"
#endif

#ifndef PARSERS_INVESTIGATION_MARKOV_SUPPLEMENT_PATH
#error "PARSERS_INVESTIGATION_MARKOV_SUPPLEMENT_PATH must identify a corpus"
#endif

#ifndef PARSERS_INVESTIGATION_MARKOV_ATTESTED_PATH
#error "PARSERS_INVESTIGATION_MARKOV_ATTESTED_PATH must identify a corpus"
#endif

namespace {

constexpr double score_comparison_epsilon{1.0e-12};
constexpr std::uint64_t fnv1a_64_offset_basis{14'695'981'039'346'656'037ULL};
constexpr std::uint64_t fnv1a_64_prime{1'099'511'628'211ULL};
constexpr std::string_view surface_linearization_name{"surface"};
constexpr std::string_view parser_canonical_linearization_name{
    "parser-canonical"};
constexpr std::string_view hybrid_linearization_name{"hybrid"};

enum class StateProjection { part, morphology };
enum class TrainingTier {
    verified_gold,
    attested_gold,
    treebank_gold,
    synthetic_gold,
    preferred_lemma_silver
};
enum class EvaluationPolicy { leave_one_out, in_sample, exposure_curve };
enum class EvaluationTier { verified, attested, treebank, all };
enum class TrainingControl {
    observed,
    shuffle_within_sequence,
    counterfactual_analysis
};

// FNV-1a is defined modulo 2^64. Keep the wrap explicit so unsigned-integer
// overflow sanitizers do not report the algorithm's intentional reduction.
[[nodiscard]] constexpr std::uint64_t
fnv1a_wrap_multiply(const std::uint64_t left,
                    const std::uint64_t right) noexcept {
    const auto left_low = static_cast<std::uint32_t>(left);
    const auto left_high = static_cast<std::uint32_t>(left >> 32U);
    const auto right_low = static_cast<std::uint32_t>(right);
    const auto right_high = static_cast<std::uint32_t>(right >> 32U);
    const auto low_product = static_cast<std::uint64_t>(left_low) * right_low;
    const auto middle =
        static_cast<std::uint64_t>(static_cast<std::uint32_t>(
            static_cast<std::uint64_t>(left_low) * right_high)) +
        static_cast<std::uint64_t>(static_cast<std::uint32_t>(
            static_cast<std::uint64_t>(left_high) * right_low));
    const auto high = static_cast<std::uint32_t>((low_product >> 32U) + middle);
    return static_cast<std::uint32_t>(low_product) |
           (static_cast<std::uint64_t>(high) << 32U);
}

struct Options final {
    std::filesystem::path database{PARSERS_INVESTIGATION_WWDB_PATH};
    std::filesystem::path corpus{PARSERS_INVESTIGATION_CORPUS_PATH};
    std::filesystem::path supplemental_corpus{
        PARSERS_INVESTIGATION_MARKOV_SUPPLEMENT_PATH};
    std::filesystem::path attested_corpus{
        PARSERS_INVESTIGATION_MARKOV_ATTESTED_PATH};
    std::string dataset_id{PARSERS_INVESTIGATION_DATASET_ID};
    std::uint64_t max_product{1'000'000U};
    double alpha{0.1};
    parsers::MarkovSmoothing smoothing{parsers::MarkovSmoothing::additive};
    double backoff_strength{1.0};
    double synthetic_weight{0.5};
    double silver_weight{0.25};
    double attested_weight{1.0};
    double treebank_weight{1.0};
    double reordering_weight{0.1};
    EvaluationPolicy evaluation_policy{EvaluationPolicy::leave_one_out};
    EvaluationTier evaluation_tier{EvaluationTier::verified};
    TrainingControl training_control{TrainingControl::observed};
    std::uint64_t shuffle_seed{20'260'906U};
    std::vector<double> exposure_multipliers{0.0, 0.01, 0.025, 0.05, 0.1, 0.25,
                                             0.5, 1.0,  2.0,   4.0,  8.0};
};

struct ParsedFixture final {
    const parsers::Fixture *fixture{};
    parsers::Result parser_output;
};

struct ScoredCandidate final {
    const parsers::RankedMorphologyAnalysis *candidate{};
    double surface_score{};
    double canonical_score{};
    double markov_score{};
    parsers::MarkovScoreDiagnostics surface_diagnostics;
    parsers::MarkovScoreDiagnostics canonical_diagnostics;
};

[[nodiscard]] nlohmann::ordered_json
analysis_profile_json(const words::AnalysisOptions &options) {
    const auto &mechanisms = options.mechanisms;
    return {{"whitakerTrim", "annotate"},
            {"orthography", words::orthography_mode_name(options.orthography)},
            {"twoWords",
             options.two_words == words::TwoWordsMode::legacy_first_match
                 ? "legacy-first-match"
                 : "disabled"},
            {"mechanisms",
             {{"productiveDerivations", mechanisms.productive_derivations},
              {"prefixes", mechanisms.prefixes},
              {"suffixes", mechanisms.suffixes},
              {"tickons", mechanisms.tickons},
              {"tackons", mechanisms.tackons},
              {"packons", mechanisms.packons},
              {"syncope", mechanisms.syncope},
              {"verbalCompounds", mechanisms.verbal_compounds}}}};
}

[[nodiscard]] std::expected<std::uint64_t, std::string>
parse_unsigned(const std::string_view value) {
    std::uint64_t parsed{};
    const auto [end, error] =
        std::from_chars(value.data(), value.data() + value.size(), parsed);
    if (error != std::errc{} || end != value.data() + value.size() ||
        parsed == 0U) {
        return std::unexpected{"expected a positive integer: " +
                               std::string{value}};
    }
    return parsed;
}

[[nodiscard]] std::expected<double, std::string>
parse_positive_double(const std::string_view value) {
    double parsed{};
    const auto [end, error] =
        std::from_chars(value.data(), value.data() + value.size(), parsed);
    if (error != std::errc{} || end != value.data() + value.size() ||
        !(parsed > 0.0) || !std::isfinite(parsed)) {
        return std::unexpected{"expected a finite positive number: " +
                               std::string{value}};
    }
    return parsed;
}

[[nodiscard]] std::expected<double, std::string>
parse_weight(const std::string_view value) {
    double parsed{};
    const auto [end, error] =
        std::from_chars(value.data(), value.data() + value.size(), parsed);
    if (error != std::errc{} || end != value.data() + value.size() ||
        parsed < 0.0 || !std::isfinite(parsed)) {
        return std::unexpected{"expected a finite non-negative weight: " +
                               std::string{value}};
    }
    return parsed;
}

[[nodiscard]] std::expected<std::vector<double>, std::string>
parse_weight_list(const std::string_view value) {
    std::vector<double> result;
    std::size_t begin{};
    while (begin <= value.size()) {
        const auto end = value.find(',', begin);
        const auto item = value.substr(begin, end == std::string_view::npos
                                                  ? value.size() - begin
                                                  : end - begin);
        auto parsed = parse_weight(item);
        if (!parsed) {
            return std::unexpected{"invalid exposure multiplier: " +
                                   std::move(parsed.error())};
        }
        result.push_back(*parsed);
        if (end == std::string_view::npos) {
            break;
        }
        begin = end + 1U;
    }
    if (result.empty()) {
        return std::unexpected{"at least one exposure multiplier is required"};
    }
    std::ranges::sort(result);
    result.erase(std::ranges::unique(result).begin(), result.end());
    return result;
}

[[nodiscard]] std::expected<Options, std::string>
parse_options(const int argc, char *const argv[]) {
    Options options;
    for (int index = 1; index < argc; ++index) {
        const std::string_view argument{argv[index]};
        const auto require_value =
            [&]() -> std::expected<std::string_view, std::string> {
            if (index + 1 >= argc) {
                return std::unexpected{"missing value after " +
                                       std::string{argument}};
            }
            ++index;
            return std::string_view{argv[index]};
        };
        if (argument == "--database") {
            auto value = require_value();
            if (!value) {
                return std::unexpected{std::move(value.error())};
            }
            options.database = *value;
        } else if (argument == "--corpus") {
            auto value = require_value();
            if (!value) {
                return std::unexpected{std::move(value.error())};
            }
            options.corpus = *value;
        } else if (argument == "--supplemental-corpus") {
            auto value = require_value();
            if (!value) {
                return std::unexpected{std::move(value.error())};
            }
            options.supplemental_corpus = *value;
        } else if (argument == "--attested-corpus") {
            auto value = require_value();
            if (!value) {
                return std::unexpected{std::move(value.error())};
            }
            options.attested_corpus = *value;
        } else if (argument == "--dataset-id") {
            auto value = require_value();
            if (!value) {
                return std::unexpected{std::move(value.error())};
            }
            options.dataset_id = *value;
        } else if (argument == "--max-product") {
            auto value = require_value();
            if (!value) {
                return std::unexpected{std::move(value.error())};
            }
            auto parsed = parse_unsigned(*value);
            if (!parsed) {
                return std::unexpected{std::move(parsed.error())};
            }
            options.max_product = *parsed;
        } else if (argument == "--alpha") {
            auto value = require_value();
            if (!value) {
                return std::unexpected{std::move(value.error())};
            }
            auto parsed = parse_positive_double(*value);
            if (!parsed) {
                return std::unexpected{std::move(parsed.error())};
            }
            options.alpha = *parsed;
        } else if (argument == "--smoothing") {
            auto value = require_value();
            if (!value) {
                return std::unexpected{std::move(value.error())};
            }
            if (*value == "additive") {
                options.smoothing = parsers::MarkovSmoothing::additive;
            } else if (*value == "hierarchical-backoff") {
                options.smoothing =
                    parsers::MarkovSmoothing::hierarchical_backoff;
            } else {
                return std::unexpected{
                    "smoothing must be additive or hierarchical-backoff"};
            }
        } else if (argument == "--backoff-strength") {
            auto value = require_value();
            if (!value) {
                return std::unexpected{std::move(value.error())};
            }
            auto parsed = parse_positive_double(*value);
            if (!parsed) {
                return std::unexpected{std::move(parsed.error())};
            }
            options.backoff_strength = *parsed;
        } else if (argument == "--synthetic-weight" ||
                   argument == "--silver-weight" ||
                   argument == "--attested-weight" ||
                   argument == "--treebank-weight" ||
                   argument == "--reordering-weight") {
            const bool synthetic = argument == "--synthetic-weight";
            const bool reordering = argument == "--reordering-weight";
            auto value = require_value();
            if (!value) {
                return std::unexpected{std::move(value.error())};
            }
            auto parsed = parse_weight(*value);
            if (!parsed) {
                return std::unexpected{std::move(parsed.error())};
            }
            if (reordering) {
                options.reordering_weight = *parsed;
            } else if (argument == "--attested-weight") {
                options.attested_weight = *parsed;
            } else if (argument == "--treebank-weight") {
                options.treebank_weight = *parsed;
            } else if (synthetic) {
                options.synthetic_weight = *parsed;
            } else {
                options.silver_weight = *parsed;
            }
        } else if (argument == "--evaluation-policy") {
            auto value = require_value();
            if (!value) {
                return std::unexpected{std::move(value.error())};
            }
            if (*value == "leave-one-fixture-out") {
                options.evaluation_policy = EvaluationPolicy::leave_one_out;
            } else if (*value == "in-sample") {
                options.evaluation_policy = EvaluationPolicy::in_sample;
            } else if (*value == "exposure-curve") {
                options.evaluation_policy = EvaluationPolicy::exposure_curve;
            } else {
                return std::unexpected{
                    "evaluation policy must be leave-one-fixture-out, "
                    "in-sample or exposure-curve"};
            }
        } else if (argument == "--evaluation-tier") {
            auto value = require_value();
            if (!value) {
                return std::unexpected{std::move(value.error())};
            }
            if (*value == "verified") {
                options.evaluation_tier = EvaluationTier::verified;
            } else if (*value == "attested") {
                options.evaluation_tier = EvaluationTier::attested;
            } else if (*value == "treebank") {
                options.evaluation_tier = EvaluationTier::treebank;
            } else if (*value == "all") {
                options.evaluation_tier = EvaluationTier::all;
            } else {
                return std::unexpected{"evaluation tier must be verified, "
                                       "attested, treebank or all"};
            }
        } else if (argument == "--training-control") {
            auto value = require_value();
            if (!value) {
                return std::unexpected{std::move(value.error())};
            }
            if (*value == "observed") {
                options.training_control = TrainingControl::observed;
            } else if (*value == "shuffle-within-sequence") {
                options.training_control =
                    TrainingControl::shuffle_within_sequence;
            } else if (*value == "counterfactual-analysis") {
                options.training_control =
                    TrainingControl::counterfactual_analysis;
            } else {
                return std::unexpected{
                    "training control must be observed, "
                    "shuffle-within-sequence or counterfactual-analysis"};
            }
        } else if (argument == "--shuffle-seed") {
            auto value = require_value();
            if (!value) {
                return std::unexpected{std::move(value.error())};
            }
            auto parsed = parse_unsigned(*value);
            if (!parsed) {
                return std::unexpected{std::move(parsed.error())};
            }
            options.shuffle_seed = *parsed;
        } else if (argument == "--exposure-multipliers") {
            auto value = require_value();
            if (!value) {
                return std::unexpected{std::move(value.error())};
            }
            auto parsed = parse_weight_list(*value);
            if (!parsed) {
                return std::unexpected{std::move(parsed.error())};
            }
            options.exposure_multipliers = std::move(*parsed);
        } else if (argument == "--help" || argument == "-h") {
            std::cout
                << "usage: markov_parser_ranker [OPTIONS]\n"
                   "  --corpus FILE       v2 fixtures used as known phrases\n"
                   "  --supplemental-corpus FILE  preferred-lemma silver data\n"
                   "  --attested-corpus FILE  attested editorial-gold data\n"
                   "  --database FILE     full or search WWDB\n"
                   "  --dataset-id ID     identifier for the WWDB\n"
                   "  --max-product N     parser enumeration budget\n"
                   "  --alpha X           additive smoothing (default 0.1)\n"
                   "  --smoothing additive|hierarchical-backoff\n"
                   "  --backoff-strength X hierarchical prior weight (1.0)\n"
                   "  --synthetic-weight X  structured synthetic weight (0.5)\n"
                   "  --silver-weight X     preferred-lemma silver weight "
                   "(0.25)\n"
                   "  --attested-weight X   TLL editorial-gold weight (1.0)\n"
                   "  --treebank-weight X   LDT morphology-gold weight (1.0)\n"
                   "  --reordering-weight X dependency-preserving augmentation "
                   "(0.1)\n"
                   "  --evaluation-policy "
                   "leave-one-fixture-out|in-sample|exposure-curve\n"
                   "  --evaluation-tier verified|attested|treebank|all\n"
                   "  --training-control "
                   "observed|shuffle-within-sequence|counterfactual-analysis\n"
                   "  --shuffle-seed N deterministic negative-control seed\n"
                   "  --exposure-multipliers CSV (default "
                   "0,.01,.025,.05,.1,.25,.5,1,2,4,8)\n";
            std::exit(0);
        } else {
            return std::unexpected{"unknown option: " + std::string{argument}};
        }
    }
    return options;
}

[[nodiscard]] std::vector<std::byte>
read_database(const std::filesystem::path &path) {
    std::ifstream input{path, std::ios::binary | std::ios::ate};
    if (!input) {
        throw std::runtime_error{"cannot open WWDB image: " + path.string()};
    }
    const auto end = input.tellg();
    if (end < 0 ||
        !std::in_range<std::size_t>(static_cast<std::streamoff>(end))) {
        throw std::runtime_error{"invalid WWDB image size: " + path.string()};
    }
    std::vector<std::byte> bytes(static_cast<std::size_t>(end));
    input.seekg(0);
    if (!bytes.empty()) {
        if (bytes.size() > static_cast<std::size_t>(
                               std::numeric_limits<std::streamsize>::max())) {
            throw std::runtime_error{"WWDB image exceeds stream limits: " +
                                     path.string()};
        }
        input.read(reinterpret_cast<char *>(bytes.data()),
                   static_cast<std::streamsize>(bytes.size()));
    }
    if (!input) {
        throw std::runtime_error{"cannot read WWDB image: " + path.string()};
    }
    return bytes;
}

[[nodiscard]] bool matches_evaluation_tier(const ParsedFixture &parsed,
                                           const EvaluationTier tier) {
    if (parsed.fixture == nullptr || !parsed.fixture->gold ||
        !parsed.fixture->annotation) {
        return false;
    }
    const auto &status = parsed.fixture->annotation->status;
    const bool verified = status == "verified-didactic";
    const bool attested = status == "verified-attested";
    const bool treebank = status == "treebank-gold";
    return tier == EvaluationTier::all ||
           (tier == EvaluationTier::verified && verified) ||
           (tier == EvaluationTier::attested && attested) ||
           (tier == EvaluationTier::treebank && treebank);
}

[[nodiscard]] bool is_evaluation_phrase(const ParsedFixture &parsed,
                                        const EvaluationTier tier) {
    return matches_evaluation_tier(parsed, tier) &&
           parsed.parser_output.status == "ok" &&
           !parsed.parser_output.morphology_nbest.empty();
}

[[nodiscard]] nlohmann::ordered_json
evaluation_coverage(const std::vector<ParsedFixture> &parsed_fixtures,
                    const EvaluationTier tier) {
    std::size_t requested{};
    std::size_t lexical_coverage{};
    std::size_t within_budget{};
    std::size_t parser_ok{};
    std::size_t nonempty_nbest{};
    std::size_t gold_in_lattice{};
    std::size_t gold_survives_constraints{};
    std::size_t rankable{};
    std::map<std::string, std::size_t, std::less<>> status_counts;
    for (const auto &parsed : parsed_fixtures) {
        if (!matches_evaluation_tier(parsed, tier)) {
            continue;
        }
        ++requested;
        ++status_counts[parsed.parser_output.status];
        const bool has_lexical_coverage =
            parsed.parser_output.candidate_counts.size() ==
                parsed.parser_output.token_count &&
            std::ranges::all_of(parsed.parser_output.candidate_counts,
                                [](const auto count) { return count != 0U; });
        lexical_coverage += static_cast<std::size_t>(has_lexical_coverage);
        within_budget += static_cast<std::size_t>(parsed.parser_output.status !=
                                                  "experiment-budget-exceeded");
        parser_ok +=
            static_cast<std::size_t>(parsed.parser_output.status == "ok");
        nonempty_nbest += static_cast<std::size_t>(
            !parsed.parser_output.morphology_nbest.empty());
        gold_in_lattice += static_cast<std::size_t>(
            parsed.parser_output.morphology_gold_in_lattice);
        gold_survives_constraints += static_cast<std::size_t>(
            parsed.parser_output.morphology_gold_survives);
        const bool contains_rankable_gold =
            parsed.parser_output.status == "ok" &&
            parsed.parser_output.morphology_gold_rank.has_value() &&
            std::ranges::any_of(parsed.parser_output.morphology_nbest,
                                [](const auto &candidate) {
                                    return candidate.matches_morphology_gold;
                                });
        rankable += static_cast<std::size_t>(contains_rankable_gold);
    }
    return {
        {"requestedGoldFixtures", requested},
        {"lexicalCoverage", lexical_coverage},
        {"withinBudget", within_budget},
        {"parserOk", parser_ok},
        {"nonemptyNBest", nonempty_nbest},
        {"goldInLattice", gold_in_lattice},
        {"goldSurvivesConstraints", gold_survives_constraints},
        {"rankable", rankable},
        {"parserStatusCounts", status_counts},
    };
}

[[nodiscard]] std::optional<TrainingTier>
training_tier(const ParsedFixture &parsed) {
    if (parsed.fixture == nullptr || parsed.parser_output.status != "ok" ||
        parsed.parser_output.morphology_nbest.empty()) {
        return std::nullopt;
    }
    if (parsed.fixture->gold && parsed.fixture->annotation &&
        parsed.fixture->annotation->status == "verified-didactic") {
        return TrainingTier::verified_gold;
    }
    if (parsed.fixture->gold && parsed.fixture->annotation &&
        parsed.fixture->annotation->status == "verified-attested") {
        return TrainingTier::attested_gold;
    }
    if (parsed.fixture->gold && parsed.fixture->annotation &&
        parsed.fixture->annotation->status == "treebank-gold") {
        return TrainingTier::treebank_gold;
    }
    if (parsed.fixture->gold) {
        return TrainingTier::synthetic_gold;
    }
    if (!parsed.fixture->preferred_lemmas.empty()) {
        return TrainingTier::preferred_lemma_silver;
    }
    return std::nullopt;
}

[[nodiscard]] std::string_view
training_tier_name(const TrainingTier tier) noexcept {
    if (tier == TrainingTier::verified_gold) {
        return "verifiedGold";
    }
    if (tier == TrainingTier::attested_gold) {
        return "attestedGold";
    }
    if (tier == TrainingTier::treebank_gold) {
        return "treebankGold";
    }
    if (tier == TrainingTier::synthetic_gold) {
        return "syntheticGold";
    }
    return "preferredLemmaSilver";
}

[[nodiscard]] double training_tier_weight(const TrainingTier tier,
                                          const double attested_weight,
                                          const double treebank_weight,
                                          const double synthetic_weight,
                                          const double silver_weight) noexcept {
    if (tier == TrainingTier::verified_gold) {
        return 1.0;
    }
    if (tier == TrainingTier::attested_gold) {
        return attested_weight;
    }
    if (tier == TrainingTier::treebank_gold) {
        return treebank_weight;
    }
    return tier == TrainingTier::synthetic_gold ? synthetic_weight
                                                : silver_weight;
}

[[nodiscard]] std::string_view
evaluation_policy_name(const EvaluationPolicy policy) noexcept {
    if (policy == EvaluationPolicy::leave_one_out) {
        return "leave-one-fixture-out";
    }
    return policy == EvaluationPolicy::in_sample ? "in-sample"
                                                 : "exposure-curve";
}

[[nodiscard]] std::string_view
training_control_name(const TrainingControl control) noexcept {
    if (control == TrainingControl::observed) {
        return "observed";
    }
    return control == TrainingControl::shuffle_within_sequence
               ? "shuffle-within-sequence"
               : "counterfactual-analysis";
}

[[nodiscard]] constexpr std::string_view
linearization_name(const double surface_weight) noexcept {
    if (surface_weight == 1.0) {
        return surface_linearization_name;
    }
    if (surface_weight == 0.0) {
        return parser_canonical_linearization_name;
    }
    return hybrid_linearization_name;
}

[[nodiscard]] constexpr std::string_view
training_control_policy(const TrainingControl control) noexcept {
    switch (std::to_underlying(control)) {
    case std::to_underlying(TrainingControl::observed):
        return "train observed selected state sequences";
    case std::to_underlying(TrainingControl::shuffle_within_sequence):
        return "deterministically shuffle states within each selected sequence";
    case std::to_underlying(TrainingControl::counterfactual_analysis):
        return "select the best manual-score non-gold candidate with a "
               "projected "
               "sequence distinct from every gold sequence; skip fixtures "
               "without one";
    default:
        return {};
    }
}

[[nodiscard]] std::string_view
evaluation_tier_name(const EvaluationTier tier) noexcept {
    if (tier == EvaluationTier::verified) {
        return "verified";
    }
    if (tier == EvaluationTier::attested) {
        return "attested";
    }
    return tier == EvaluationTier::treebank ? "treebank" : "all";
}

[[nodiscard]] std::string_view
projection_name(const StateProjection projection) noexcept {
    return projection == StateProjection::part ? "part" : "part+morphology";
}

[[nodiscard]] std::string_view
smoothing_name(const parsers::MarkovSmoothing smoothing) noexcept {
    return smoothing == parsers::MarkovSmoothing::additive
               ? "additive"
               : "hierarchical-backoff";
}

[[nodiscard]] nlohmann::ordered_json
diagnostics_json(const parsers::MarkovScoreDiagnostics &diagnostics) {
    return {
        {"transitions", diagnostics.transitions},
        {"unknownStates", diagnostics.unknown_states},
        {"fullContextHits", diagnostics.full_context_hits},
        {"fullContextMisses", diagnostics.full_context_misses},
        {"uniformFallbacks", diagnostics.uniform_fallbacks},
        {"backedOffTransitions", diagnostics.backed_off_transitions},
        {"noObservedContext", diagnostics.no_observed_context},
        {"deepestObservedContextHits",
         diagnostics.deepest_observed_context_hits},
    };
}

[[nodiscard]] std::vector<std::string>
surface_states(const parsers::RankedMorphologyAnalysis &analysis,
               const StateProjection projection) {
    std::vector<std::string> result;
    result.reserve(analysis.analysis.size());
    for (const auto &token : analysis.analysis) {
        if (projection == StateProjection::part) {
            result.push_back(token.part);
        } else {
            result.push_back(token.part + ':' + token.morphology);
        }
    }
    return result;
}

[[nodiscard]] std::vector<std::string>
canonical_states(const parsers::RankedMorphologyAnalysis &analysis,
                 const StateProjection projection) {
    const auto token_count = analysis.analysis.size();
    std::vector<std::string> base_states;
    base_states.reserve(token_count);
    for (const auto &token : analysis.analysis) {
        base_states.push_back(projection == StateProjection::part
                                  ? token.part
                                  : token.part + ':' + token.morphology);
    }

    std::vector<std::string> result;
    result.reserve(token_count * 2U);
    const auto events = parsers::markov::parser_canonical_events(
        analysis, [&](const auto token) -> const std::string & {
            return base_states[token];
        });
    for (const auto &event : events) {
        if (event.kind == parsers::markov::CanonicalEventKind::enter) {
            result.push_back("enter|" + event.incoming_label + '|' +
                             base_states[event.token]);
        } else {
            result.push_back("exit|" + event.incoming_label);
        }
    }
    return result;
}

[[nodiscard]] std::vector<std::string>
parser_ordered_surface_states(const parsers::RankedMorphologyAnalysis &analysis,
                              const StateProjection projection,
                              const parsers::markov::TraversalOrder traversal) {
    const auto base = surface_states(analysis, projection);
    std::vector<std::string> result;
    result.reserve(base.size());
    for (const auto &entry : parsers::markov::parser_canonical_order(
             analysis,
             [&](const auto token) -> const std::string & {
                 return base[token];
             },
             traversal)) {
        result.push_back(base[entry.token]);
    }
    return result;
}

using StateSequencePair =
    std::pair<std::vector<std::string>, std::vector<std::string>>;

[[nodiscard]] std::set<StateSequencePair>
training_sequences(const ParsedFixture &parsed, const TrainingTier tier,
                   const StateProjection projection,
                   const TrainingControl control) {
    std::set<StateSequencePair> result;
    std::set<StateSequencePair> gold_sequences;
    std::optional<double> best_silver_score;
    std::optional<double> best_counterfactual_score;
    if (control == TrainingControl::counterfactual_analysis) {
        for (const auto &candidate : parsed.parser_output.morphology_nbest) {
            if (candidate.matches_morphology_gold) {
                gold_sequences.emplace(surface_states(candidate, projection),
                                       canonical_states(candidate, projection));
            }
        }
    }
    if (tier == TrainingTier::preferred_lemma_silver) {
        for (const auto &candidate : parsed.parser_output.morphology_nbest) {
            if (candidate.matches_preferred_lemmas &&
                (!best_silver_score ||
                 candidate.manual_score > *best_silver_score)) {
                best_silver_score = candidate.manual_score;
            }
        }
    }
    if (control == TrainingControl::counterfactual_analysis &&
        tier != TrainingTier::preferred_lemma_silver) {
        for (const auto &candidate : parsed.parser_output.morphology_nbest) {
            const StateSequencePair sequence{
                surface_states(candidate, projection),
                canonical_states(candidate, projection)};
            if (!candidate.matches_morphology_gold &&
                !gold_sequences.contains(sequence) &&
                (!best_counterfactual_score ||
                 candidate.manual_score > *best_counterfactual_score)) {
                best_counterfactual_score = candidate.manual_score;
            }
        }
    }
    for (const auto &candidate : parsed.parser_output.morphology_nbest) {
        bool selected{};
        if (tier == TrainingTier::preferred_lemma_silver) {
            selected = candidate.matches_preferred_lemmas &&
                       best_silver_score &&
                       std::abs(candidate.manual_score - *best_silver_score) <
                           score_comparison_epsilon;
        } else if (control == TrainingControl::counterfactual_analysis) {
            const StateSequencePair sequence{
                surface_states(candidate, projection),
                canonical_states(candidate, projection)};
            selected =
                !candidate.matches_morphology_gold &&
                !gold_sequences.contains(sequence) &&
                best_counterfactual_score &&
                std::abs(candidate.manual_score - *best_counterfactual_score) <
                    score_comparison_epsilon;
        } else {
            selected = candidate.matches_morphology_gold;
        }
        if (selected) {
            result.emplace(surface_states(candidate, projection),
                           canonical_states(candidate, projection));
        }
    }
    return result;
}

[[nodiscard]] std::set<std::vector<std::string>>
synthetic_relinearizations(const ParsedFixture &parsed, const TrainingTier tier,
                           const StateProjection projection,
                           const TrainingControl control) {
    std::set<std::vector<std::string>> result;
    if (tier == TrainingTier::preferred_lemma_silver) {
        return result;
    }
    std::optional<double> best_counterfactual_score;
    std::set<StateSequencePair> gold_sequences;
    if (control == TrainingControl::counterfactual_analysis) {
        for (const auto &candidate : parsed.parser_output.morphology_nbest) {
            if (candidate.matches_morphology_gold) {
                gold_sequences.emplace(surface_states(candidate, projection),
                                       canonical_states(candidate, projection));
            }
        }
        for (const auto &candidate : parsed.parser_output.morphology_nbest) {
            const StateSequencePair sequence{
                surface_states(candidate, projection),
                canonical_states(candidate, projection)};
            if (!candidate.matches_morphology_gold &&
                !gold_sequences.contains(sequence) &&
                (!best_counterfactual_score ||
                 candidate.manual_score > *best_counterfactual_score)) {
                best_counterfactual_score = candidate.manual_score;
            }
        }
    }
    for (const auto &candidate : parsed.parser_output.morphology_nbest) {
        const StateSequencePair sequence{
            surface_states(candidate, projection),
            canonical_states(candidate, projection)};
        const bool selected =
            control == TrainingControl::counterfactual_analysis
                ? !candidate.matches_morphology_gold &&
                      !gold_sequences.contains(sequence) &&
                      best_counterfactual_score &&
                      std::abs(candidate.manual_score -
                               *best_counterfactual_score) <
                          score_comparison_epsilon
                : candidate.matches_morphology_gold;
        if (!selected) {
            continue;
        }
        const auto original = surface_states(candidate, projection);
        for (const auto traversal :
             {parsers::markov::TraversalOrder::head_first,
              parsers::markov::TraversalOrder::head_last}) {
            auto reordered =
                parser_ordered_surface_states(candidate, projection, traversal);
            if (reordered != original) {
                result.insert(std::move(reordered));
            }
        }
    }
    return result;
}

[[nodiscard]] std::uint64_t
stable_shuffle_seed(const std::uint64_t seed, const std::string_view fixture_id,
                    const std::string_view channel,
                    const std::size_t ordinal) noexcept {
    // FNV-1a is used only to make the negative control reproducible across
    // standard-library implementations; this is not a cryptographic hash.
    std::uint64_t hash = fnv1a_64_offset_basis ^ seed;
    const auto append = [&](const std::string_view text) {
        for (const auto byte : text) {
            hash ^= static_cast<unsigned char>(byte);
            hash = fnv1a_wrap_multiply(hash, fnv1a_64_prime);
        }
    };
    append(fixture_id);
    append(channel);
    hash ^= static_cast<std::uint64_t>(ordinal);
    hash = fnv1a_wrap_multiply(hash, fnv1a_64_prime);
    return hash;
}

[[nodiscard]] std::vector<std::string>
controlled_sequence(const std::vector<std::string> &sequence,
                    const TrainingControl control, const std::uint64_t seed,
                    const std::string_view fixture_id,
                    const std::string_view channel, const std::size_t ordinal) {
    if (control != TrainingControl::shuffle_within_sequence ||
        sequence.size() < 2U) {
        return sequence;
    }
    auto shuffled = sequence;
    std::mt19937_64 generator{
        stable_shuffle_seed(seed, fixture_id, channel, ordinal)};
    std::ranges::shuffle(shuffled, generator);
    return shuffled;
}

[[nodiscard]] nlohmann::ordered_json evaluate(
    const std::vector<ParsedFixture> &parsed_fixtures, const std::size_t order,
    const StateProjection projection, const double surface_weight,
    const double alpha, const parsers::MarkovSmoothing smoothing,
    const double backoff_strength, const double attested_weight,
    const double treebank_weight, const double synthetic_weight,
    const double silver_weight, const double reordering_weight,
    const EvaluationPolicy evaluation_policy,
    const EvaluationTier evaluation_tier,
    const TrainingControl training_control, const std::uint64_t shuffle_seed,
    const double target_exposure_multiplier) {
    using Json = nlohmann::ordered_json;
    Json per_fixture = Json::array();
    std::size_t evaluated{};
    std::size_t markov_top1{};
    std::size_t markov_top3{};
    std::size_t markov_best_score_ties{};
    std::size_t markov_strict_top1{};
    std::size_t manual_top1{};
    std::size_t manual_top3{};
    std::size_t markov_only_top1{};
    std::size_t markov_only_top3{};
    double manual_reciprocal_rank_sum{};
    double markov_only_reciprocal_rank_sum{};
    double reciprocal_rank_sum{};
    parsers::MarkovScoreDiagnostics surface_diagnostics;
    parsers::MarkovScoreDiagnostics canonical_diagnostics;

    for (const auto &target : parsed_fixtures) {
        if (!is_evaluation_phrase(target, evaluation_tier)) {
            continue;
        }

        parsers::MarkovModel surface_model{order, alpha, smoothing,
                                           backoff_strength};
        parsers::MarkovModel canonical_model{order, alpha, smoothing,
                                             backoff_strength};
        std::size_t training_phrases{};
        std::size_t training_sequence_count{};
        std::size_t relinearization_sequence_count{};
        std::size_t training_control_skipped_phrases{};
        std::map<std::string, std::size_t, std::less<>> training_by_tier;
        for (const auto &training : parsed_fixtures) {
            const auto tier = training_tier(training);
            const bool same_target = training.fixture->id == target.fixture->id;
            const bool same_text =
                training.fixture->text == target.fixture->text;
            const bool leave_one_out =
                evaluation_policy == EvaluationPolicy::leave_one_out &&
                (same_target || same_text);
            const bool exposure_duplicate =
                evaluation_policy == EvaluationPolicy::exposure_curve &&
                same_text && !same_target;
            const bool zero_exposure =
                evaluation_policy == EvaluationPolicy::exposure_curve &&
                same_target && target_exposure_multiplier == 0.0;
            if (!tier || leave_one_out || exposure_duplicate || zero_exposure) {
                continue;
            }
            auto phrase_weight =
                training_tier_weight(*tier, attested_weight, treebank_weight,
                                     synthetic_weight, silver_weight);
            if (evaluation_policy == EvaluationPolicy::exposure_curve &&
                same_target) {
                phrase_weight *= target_exposure_multiplier;
            }
            const auto alternatives = training_sequences(
                training, *tier, projection, training_control);
            if (alternatives.empty()) {
                if (training_control ==
                    TrainingControl::counterfactual_analysis) {
                    ++training_control_skipped_phrases;
                }
                continue;
            }
            if (phrase_weight == 0.0) {
                continue;
            }
            const auto weight =
                phrase_weight / static_cast<double>(alternatives.size());
            std::size_t sequence_ordinal{};
            for (const auto &[surface, canonical] : alternatives) {
                surface_model.train(
                    controlled_sequence(surface, training_control, shuffle_seed,
                                        training.fixture->id, "surface",
                                        sequence_ordinal),
                    weight);
                canonical_model.train(
                    controlled_sequence(canonical, training_control,
                                        shuffle_seed, training.fixture->id,
                                        "canonical", sequence_ordinal),
                    weight);
                ++training_sequence_count;
                ++sequence_ordinal;
            }
            const auto relinearizations = synthetic_relinearizations(
                training, *tier, projection, training_control);
            if (!relinearizations.empty() && reordering_weight > 0.0) {
                const auto augmented_weight =
                    phrase_weight * reordering_weight /
                    static_cast<double>(relinearizations.size());
                std::size_t relinearization_ordinal{};
                for (const auto &surface : relinearizations) {
                    surface_model.train(
                        controlled_sequence(surface, training_control,
                                            shuffle_seed, training.fixture->id,
                                            "relinearization",
                                            relinearization_ordinal),
                        augmented_weight);
                    ++relinearization_sequence_count;
                    ++relinearization_ordinal;
                }
            }
            ++training_phrases;
            ++training_by_tier[std::string{training_tier_name(*tier)}];
        }
        if (training_phrases == 0U) {
            continue;
        }

        std::vector<ScoredCandidate> candidates;
        candidates.reserve(target.parser_output.morphology_nbest.size());
        for (const auto &candidate : target.parser_output.morphology_nbest) {
            const auto surface =
                surface_model.score(surface_states(candidate, projection));
            const auto canonical =
                canonical_model.score(canonical_states(candidate, projection));
            surface_diagnostics.merge(surface.diagnostics);
            canonical_diagnostics.merge(canonical.diagnostics);
            candidates.push_back(ScoredCandidate{
                .candidate = &candidate,
                .surface_score = surface.log_probability,
                .canonical_score = canonical.log_probability,
                .markov_score =
                    surface_weight * surface.log_probability +
                    (1.0 - surface_weight) * canonical.log_probability,
                .surface_diagnostics = surface.diagnostics,
                .canonical_diagnostics = canonical.diagnostics,
            });
        }
        auto markov_only_candidates = candidates;
        std::ranges::stable_sort(
            markov_only_candidates,
            [](const ScoredCandidate &left, const ScoredCandidate &right) {
                if (left.markov_score > right.markov_score) {
                    return true;
                }
                if (right.markov_score > left.markov_score) {
                    return false;
                }
                return left.candidate->assignment_id <
                       right.candidate->assignment_id;
            });
        std::ranges::stable_sort(candidates, [](const ScoredCandidate &left,
                                                const ScoredCandidate &right) {
            if (left.markov_score > right.markov_score) {
                return true;
            }
            if (right.markov_score > left.markov_score) {
                return false;
            }
            if (left.candidate->manual_score > right.candidate->manual_score) {
                return true;
            }
            if (right.candidate->manual_score > left.candidate->manual_score) {
                return false;
            }
            return left.candidate->assignment_id <
                   right.candidate->assignment_id;
        });

        const auto find_gold_rank = [](const auto &ranked) {
            std::optional<std::size_t> result;
            for (std::size_t index = 0; index < ranked.size(); ++index) {
                if (ranked[index].candidate->matches_morphology_gold) {
                    result = index + 1U;
                    break;
                }
            }
            return result;
        };

        const auto gold_rank = find_gold_rank(candidates);
        const auto markov_only_gold_rank =
            find_gold_rank(markov_only_candidates);
        std::optional<double> best_gold_score;
        for (std::size_t index = 0; index < candidates.size(); ++index) {
            if (!candidates[index].candidate->matches_morphology_gold) {
                continue;
            }
            if (!best_gold_score ||
                candidates[index].markov_score > *best_gold_score) {
                best_gold_score = candidates[index].markov_score;
            }
        }
        if (!gold_rank || !markov_only_gold_rank || !best_gold_score ||
            !target.parser_output.morphology_gold_rank) {
            continue;
        }

        ++evaluated;
        markov_top1 += static_cast<std::size_t>(*gold_rank == 1U);
        markov_top3 += static_cast<std::size_t>(*gold_rank <= 3U);
        markov_only_top1 +=
            static_cast<std::size_t>(*markov_only_gold_rank == 1U);
        markov_only_top3 +=
            static_cast<std::size_t>(*markov_only_gold_rank <= 3U);
        manual_top1 += static_cast<std::size_t>(
            *target.parser_output.morphology_gold_rank == 1U);
        manual_top3 += static_cast<std::size_t>(
            *target.parser_output.morphology_gold_rank <= 3U);
        manual_reciprocal_rank_sum +=
            1.0 /
            static_cast<double>(*target.parser_output.morphology_gold_rank);
        markov_only_reciprocal_rank_sum +=
            1.0 / static_cast<double>(*markov_only_gold_rank);
        reciprocal_rank_sum += 1.0 / static_cast<double>(*gold_rank);
        const bool best_score_tie =
            std::abs(*best_gold_score - candidates.front().markov_score) <
            score_comparison_epsilon;
        const auto top_score_candidates = static_cast<std::size_t>(
            std::ranges::count_if(candidates, [&](const auto &candidate) {
                return std::abs(candidate.markov_score -
                                candidates.front().markov_score) <
                       score_comparison_epsilon;
            }));
        const auto top_score_gold_candidates = static_cast<std::size_t>(
            std::ranges::count_if(candidates, [&](const auto &candidate) {
                return candidate.candidate->matches_morphology_gold &&
                       std::abs(candidate.markov_score -
                                candidates.front().markov_score) <
                           score_comparison_epsilon;
            }));
        const bool strict_top1 =
            top_score_candidates != 0U &&
            top_score_candidates == top_score_gold_candidates;
        markov_best_score_ties += static_cast<std::size_t>(best_score_tie);
        markov_strict_top1 += static_cast<std::size_t>(strict_top1);

        per_fixture.push_back(
            {{"fixtureId", target.fixture->id},
             {"text", target.fixture->text},
             {"parserCandidates", candidates.size()},
             {"trainingPhrases", training_phrases},
             {"trainingSequences", training_sequence_count},
             {"syntheticRelinearizationSequences",
              relinearization_sequence_count},
             {"trainingControlSkippedPhrases",
              training_control_skipped_phrases},
             {"trainingPhrasesByTier", training_by_tier},
             {"manualGoldRank", *target.parser_output.morphology_gold_rank},
             {"markovOnlyGoldRank", *markov_only_gold_rank},
             {"markovThenManualGoldRank", *gold_rank},
             {"markovGoldRank", *gold_rank},
             {"markovGoldBestScoreTie", best_score_tie},
             {"topScoreCandidates", top_score_candidates},
             {"topScoreGoldCandidates", top_score_gold_candidates},
             {"markovStrictTop1", strict_top1},
             {"markovOnlyTopAssignmentId",
              markov_only_candidates.front().candidate->assignment_id},
             {"markovThenManualTopAssignmentId",
              candidates.front().candidate->assignment_id},
             {"topAssignmentId", candidates.front().candidate->assignment_id},
             {"topSurfaceStates",
              surface_states(*candidates.front().candidate, projection)},
             {"topParserOrderedStates",
              canonical_states(*candidates.front().candidate, projection)},
             {"topSurfaceLogScore", candidates.front().surface_score},
             {"topParserOrderedLogScore", candidates.front().canonical_score},
             {"topMarkovLogScore", candidates.front().markov_score},
             {"topManualScore", candidates.front().candidate->manual_score},
             {"topSurfaceDiagnostics",
              diagnostics_json(candidates.front().surface_diagnostics)},
             {"topParserOrderedDiagnostics",
              diagnostics_json(candidates.front().canonical_diagnostics)}});
    }

    return Json{
        {"order", order},
        {"stateProjection", projection_name(projection)},
        {"linearization", linearization_name(surface_weight)},
        {"surfaceWeight", surface_weight},
        {"parserCanonicalWeight", 1.0 - surface_weight},
        {"targetExposureMultiplier", target_exposure_multiplier},
        {"smoothing",
         {{"kind", smoothing_name(smoothing)},
          {"alpha", alpha},
          {"backoffStrength", backoff_strength}}},
        {"summary",
         {{"evaluated", evaluated},
          {"manualTop1", manual_top1},
          {"markovTop1", markov_top1},
          {"markovTop3", markov_top3},
          {"markovBestScoreTies", markov_best_score_ties},
          {"markovStrictTop1", markov_strict_top1},
          {"markovMRR", evaluated == 0U ? 0.0
                                        : reciprocal_rank_sum /
                                              static_cast<double>(evaluated)},
          {"scoringDiagnostics",
           {{"surface", diagnostics_json(surface_diagnostics)},
            {"parserCanonical", diagnostics_json(canonical_diagnostics)}}},
          {"rankingAblation",
           {{"manualOnly",
             {{"top1", manual_top1},
              {"top3", manual_top3},
              {"mrr", evaluated == 0U ? 0.0
                                      : manual_reciprocal_rank_sum /
                                            static_cast<double>(evaluated)}}},
            {"markovOnly",
             {{"top1", markov_only_top1},
              {"top3", markov_only_top3},
              {"mrr", evaluated == 0U ? 0.0
                                      : markov_only_reciprocal_rank_sum /
                                            static_cast<double>(evaluated)},
              {"tieBreak", "assignment-id"}}},
            {"markovThenManual",
             {{"top1", markov_top1},
              {"top3", markov_top3},
              {"mrr", evaluated == 0U ? 0.0
                                      : reciprocal_rank_sum /
                                            static_cast<double>(evaluated)},
              {"tieBreak", "manual-score-then-assignment-id"}}}}}}},
        {"fixtures", std::move(per_fixture)},
    };
}

} // namespace

int main(const int argc, char *argv[]) try {
    const auto options = parse_options(argc, argv);
    if (!options) {
        std::cerr << "markov_parser_ranker: " << options.error() << '\n';
        return 2;
    }

    auto engine =
        words::Engine::create(read_database(options->database),
                              words::EngineConfig{options->dataset_id});
    if (!engine) {
        std::cerr << "markov_parser_ranker: " << engine.error().code << ": "
                  << engine.error().message << '\n';
        return 3;
    }
    const auto fixtures = parsers::load_corpus(options->corpus);
    const auto supplemental_fixtures =
        parsers::load_corpus(options->supplemental_corpus);
    const auto attested_fixtures =
        parsers::load_corpus(options->attested_corpus);
    const parsers::Experiment experiment{**engine, options->max_product};
    std::vector<ParsedFixture> parsed;
    parsed.reserve(fixtures.size() + supplemental_fixtures.size() +
                   attested_fixtures.size());
    for (const auto &fixture : fixtures) {
        parsed.push_back(ParsedFixture{
            .fixture = &fixture,
            .parser_output = experiment.run(
                fixture, parsers::Strategy::dependency_projection),
        });
    }
    for (const auto &fixture : supplemental_fixtures) {
        parsed.push_back(ParsedFixture{
            .fixture = &fixture,
            .parser_output = experiment.run(
                fixture, parsers::Strategy::dependency_projection),
        });
    }
    for (const auto &fixture : attested_fixtures) {
        parsed.push_back(ParsedFixture{
            .fixture = &fixture,
            .parser_output = experiment.run(
                fixture, parsers::Strategy::dependency_projection),
        });
    }

    using Json = nlohmann::ordered_json;
    Json configurations = Json::array();
    constexpr std::array projections{StateProjection::part,
                                     StateProjection::morphology};
    constexpr std::array surface_weights{1.0, 0.5, 0.0};
    const std::vector<double> exposure_multipliers =
        options->evaluation_policy == EvaluationPolicy::exposure_curve
            ? options->exposure_multipliers
            : std::vector<double>{options->evaluation_policy ==
                                          EvaluationPolicy::in_sample
                                      ? 1.0
                                      : 0.0};
    for (const auto exposure_multiplier : exposure_multipliers) {
        for (const auto projection : projections) {
            for (std::size_t order = 1U; order <= 2U; ++order) {
                for (const auto surface_weight : surface_weights) {
                    configurations.push_back(evaluate(
                        parsed, order, projection, surface_weight,
                        options->alpha, options->smoothing,
                        options->backoff_strength, options->attested_weight,
                        options->treebank_weight, options->synthetic_weight,
                        options->silver_weight, options->reordering_weight,
                        options->evaluation_policy, options->evaluation_tier,
                        options->training_control, options->shuffle_seed,
                        exposure_multiplier));
                }
            }
        }
    }

    const Json output{
        {"schema", "words-parser-markov-investigation"},
        {"schemaVersion", 3},
        {"datasetId", options->dataset_id},
        {"sourceCommit", PARSERS_INVESTIGATION_GIT_COMMIT},
        {"compiler", PARSERS_INVESTIGATION_COMPILER},
        {"compilerVersion", PARSERS_INVESTIGATION_COMPILER_VERSION},
        {"buildType", PARSERS_INVESTIGATION_BUILD_TYPE},
        {"maxProduct", options->max_product},
        {"analysisProfile",
         analysis_profile_json(parsed.front().parser_output.analysis_options)},
        {"parserStrategy", "dependency-projection"},
        {"candidatePolicy",
         "all morphology assignments surviving hard constraints"},
        {"structurePolicy",
         "one deterministic dependency projection per morphology assignment; "
         "decoder tree alternatives are not consumed"},
        {"trainingCorpus", "tiered verified, attested editorial, treebank, "
                           "synthetic and preferred-lemma silver fixtures"},
        {"supplementalCorpus", options->supplemental_corpus.string()},
        {"attestedCorpus", options->attested_corpus.string()},
        {"trainingWeights",
         {{"verifiedGold", 1.0},
          {"attestedGold", options->attested_weight},
          {"treebankGold", options->treebank_weight},
          {"syntheticGold", options->synthetic_weight},
          {"preferredLemmaSilver", options->silver_weight},
          {"syntheticRelinearization", options->reordering_weight}}},
        {"evaluationPolicy",
         evaluation_policy_name(options->evaluation_policy)},
        {"evaluationTier", evaluation_tier_name(options->evaluation_tier)},
        {"evaluationCoverage",
         evaluation_coverage(parsed, options->evaluation_tier)},
        {"trainingControl", training_control_name(options->training_control)},
        {"trainingControlPolicy",
         training_control_policy(options->training_control)},
        {"shuffleSeed", options->shuffle_seed},
        {"exposureMultipliers", exposure_multipliers},
        {"parserCanonicalOrder",
         "root-first DFS; siblings sorted by recursive labeled-subtree "
         "signature; token only breaks structurally indistinguishable ties"},
        {"parserCanonicalEncoding",
         "enter relation+state and exit relation events retain subtree "
         "boundaries"},
        {"syntheticRelinearizationPolicy",
         "head-first and head-last parser traversals train only the surface "
         "model"},
        {"goldAlternativeWeighting",
         "one unit per phrase divided equally among distinct state sequences"},
        {"silverSelection",
         "best manual-score tie among preferred-lemma-compatible parses"},
        {"tieBreak", "manual-score-then-assignment-id"},
        {"rankingAblation", "manual-only, Markov-only with assignment-id ties, "
                            "and Markov-then-manual"},
        {"configurations", std::move(configurations)},
    };
    std::cout << output.dump(2) << '\n';
    return 0;
} catch (const std::exception &error) {
    std::cerr << "markov_parser_ranker: " << error.what() << '\n';
    return 4;
}
