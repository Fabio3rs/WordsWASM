#include "dependency_markov.hpp"
#include "parser.hpp"

#include "words/engine.hpp"

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
#include <map>
#include <optional>
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

namespace {

using Json = nlohmann::ordered_json;

enum class Projection { part, morphology };
enum class EvaluationPolicy { leave_one_out, in_sample };

struct Options final {
    std::filesystem::path database{PARSERS_INVESTIGATION_WWDB_PATH};
    std::filesystem::path corpus{PARSERS_INVESTIGATION_CORPUS_PATH};
    std::string dataset_id{PARSERS_INVESTIGATION_DATASET_ID};
    std::uint64_t max_product{1'000'000U};
    double alpha{0.1};
    double backoff_strength{1.0};
    double synthetic_weight{0.5};
    EvaluationPolicy evaluation_policy{EvaluationPolicy::leave_one_out};
};

struct ParsedFixture final {
    const parsers::Fixture *fixture{};
    parsers::Result parser_output;
};

struct ScoredCandidate final {
    const parsers::RankedDependencyTreeAnalysis *candidate{};
    double score{};
    parsers::dependency_markov::Diagnostics diagnostics;
};

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
parse_nonnegative_double(const std::string_view value) {
    double parsed{};
    const auto [end, error] =
        std::from_chars(value.data(), value.data() + value.size(), parsed);
    if (error != std::errc{} || end != value.data() + value.size() ||
        parsed < 0.0 || !std::isfinite(parsed)) {
        return std::unexpected{"expected a finite non-negative number: " +
                               std::string{value}};
    }
    return parsed;
}

[[nodiscard]] std::expected<Options, std::string> parse_options(const int argc,
                                                                char *argv[]) {
    Options options;
    for (int index = 1; index < argc; ++index) {
        const std::string_view argument{argv[index]};
        const auto require_value =
            [&]() -> std::expected<std::string_view, std::string> {
            if (index + 1 >= argc) {
                return std::unexpected{"missing value after " +
                                       std::string{argument}};
            }
            return std::string_view{argv[++index]};
        };
        if (argument == "--database" || argument == "--corpus" ||
            argument == "--dataset-id" || argument == "--max-product" ||
            argument == "--alpha" || argument == "--backoff-strength" ||
            argument == "--synthetic-weight") {
            const auto value = require_value();
            if (!value) {
                return std::unexpected{value.error()};
            }
            if (argument == "--database") {
                options.database = *value;
            } else if (argument == "--corpus") {
                options.corpus = *value;
            } else if (argument == "--dataset-id") {
                options.dataset_id = *value;
            } else if (argument == "--max-product") {
                const auto parsed = parse_unsigned(*value);
                if (!parsed) {
                    return std::unexpected{parsed.error()};
                }
                options.max_product = *parsed;
            } else if (argument == "--synthetic-weight") {
                const auto parsed = parse_nonnegative_double(*value);
                if (!parsed) {
                    return std::unexpected{parsed.error()};
                }
                options.synthetic_weight = *parsed;
            } else {
                const auto parsed = parse_positive_double(*value);
                if (!parsed) {
                    return std::unexpected{parsed.error()};
                }
                if (argument == "--alpha") {
                    options.alpha = *parsed;
                } else {
                    options.backoff_strength = *parsed;
                }
            }
        } else if (argument == "--in-sample") {
            options.evaluation_policy = EvaluationPolicy::in_sample;
        } else if (argument == "--help") {
            std::cout << "usage: dependency_markov_ranker [OPTIONS]\n"
                         "  --database PATH\n"
                         "  --corpus PATH\n"
                         "  --dataset-id ID\n"
                         "  --max-product N\n"
                         "  --alpha N\n"
                         "  --backoff-strength N\n"
                         "  --synthetic-weight N\n"
                         "  --in-sample\n";
            std::exit(0);
        } else {
            return std::unexpected{"unknown argument: " +
                                   std::string{argument}};
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
    const auto size = input.tellg();
    if (size < 0) {
        throw std::runtime_error{"cannot determine WWDB size: " +
                                 path.string()};
    }
    std::vector<std::byte> bytes(static_cast<std::size_t>(size));
    input.seekg(0);
    if (!bytes.empty()) {
        input.read(reinterpret_cast<char *>(bytes.data()),
                   static_cast<std::streamsize>(bytes.size()));
    }
    if (!input) {
        throw std::runtime_error{"cannot read WWDB image: " + path.string()};
    }
    return bytes;
}

[[nodiscard]] std::string_view projection_name(const Projection projection) {
    return projection == Projection::part ? "part" : "part+morphology";
}

[[nodiscard]] constexpr char
factor_kind_marker(const parsers::dependency_markov::FactorKind kind) noexcept {
    switch (std::to_underlying(kind)) {
    case std::to_underlying(parsers::dependency_markov::FactorKind::root):
        return 'R';
    case std::to_underlying(parsers::dependency_markov::FactorKind::arc):
        return 'A';
    case std::to_underlying(
        parsers::dependency_markov::FactorKind::predicate_profile):
        return 'P';
    default:
        return '?';
    }
}

[[nodiscard]] bool is_verified(const ParsedFixture &parsed) {
    return parsed.fixture != nullptr && parsed.fixture->annotation &&
           parsed.fixture->annotation->status == "verified-didactic";
}

[[nodiscard]] double training_weight(const ParsedFixture &parsed,
                                     const Options &options) {
    return is_verified(parsed) ? 1.0 : options.synthetic_weight;
}

template <typename Analysis>
[[nodiscard]] std::vector<parsers::dependency_markov::Factor>
factors(const Analysis &analysis, const Projection projection,
        const std::size_t order, const bool include_predicate_profiles) {
    return parsers::dependency_markov::extract_factors(
        analysis,
        [&](const std::size_t token) {
            const auto &choice = analysis.analysis[token];
            return projection == Projection::part
                       ? choice.part
                       : choice.part + ':' + choice.morphology;
        },
        order, include_predicate_profiles);
}

[[nodiscard]] std::string factor_signature(
    const std::vector<parsers::dependency_markov::Factor> &factors) {
    std::string signature;
    const auto append = [&](const std::string_view value) {
        signature += std::to_string(value.size());
        signature += ':';
        signature += value;
    };
    for (const auto &factor : factors) {
        signature += factor_kind_marker(factor.kind);
        append(factor.event);
        for (const auto &context : factor.contexts) {
            append(context);
        }
    }
    return signature;
}

[[nodiscard]] std::optional<std::size_t>
root_of(const std::vector<parsers::Relation> &relations) {
    const auto root = std::ranges::find_if(
        relations, [](const auto &relation) { return !relation.head; });
    return root == relations.end()
               ? std::nullopt
               : std::optional<std::size_t>{root->dependent};
}

struct AttachmentMetrics final {
    bool root_correct{};
    double uas{};
    double las{};
};

[[nodiscard]] AttachmentMetrics
attachment_metrics(const parsers::RankedDependencyTreeAnalysis &candidate,
                   const parsers::Fixture &fixture) {
    AttachmentMetrics best;
    if (!fixture.gold || fixture.gold->accepted_dependencies.empty() ||
        candidate.relations.empty()) {
        return best;
    }
    const auto candidate_root = root_of(candidate.relations);
    for (const auto &gold : fixture.gold->accepted_dependencies) {
        const auto gold_root = root_of(gold);
        best.root_correct = best.root_correct || (candidate_root && gold_root &&
                                                  candidate_root == gold_root);
        std::size_t heads{};
        std::size_t labels{};
        for (const auto &expected : gold) {
            const auto found = std::ranges::find_if(
                candidate.relations, [&](const auto &relation) {
                    return relation.dependent == expected.dependent;
                });
            if (found == candidate.relations.end() ||
                found->head != expected.head) {
                continue;
            }
            ++heads;
            labels += static_cast<std::size_t>(found->label == expected.label);
        }
        const auto denominator = static_cast<double>(gold.size());
        if (denominator != 0.0) {
            best.uas =
                std::max(best.uas, static_cast<double>(heads) / denominator);
            best.las =
                std::max(best.las, static_cast<double>(labels) / denominator);
        }
    }
    return best;
}

[[nodiscard]] std::optional<std::size_t>
gold_rank(const std::vector<ScoredCandidate> &candidates) {
    const auto found = std::ranges::find_if(candidates, [](const auto &scored) {
        return scored.candidate->matches_dependency_gold;
    });
    if (found == candidates.end()) {
        return std::nullopt;
    }
    return static_cast<std::size_t>(found - candidates.begin()) + 1U;
}

[[nodiscard]] Json
diagnostics_json(const parsers::dependency_markov::Diagnostics &diagnostics) {
    return {{"factors", diagnostics.factors},
            {"unknownEvents", diagnostics.unknown_events},
            {"fullContextHits", diagnostics.full_context_hits},
            {"fullContextMisses", diagnostics.full_context_misses},
            {"backedOffFactors", diagnostics.backed_off_factors},
            {"deepestContextHits", diagnostics.deepest_context_hits}};
}

[[nodiscard]] Json evaluate(const std::vector<ParsedFixture> &parsed,
                            const Options &options, const Projection projection,
                            const std::size_t order,
                            const bool include_predicate_profiles) {
    Json fixtures = Json::array();
    std::size_t evaluated{};
    std::size_t candidate_gold_recall{};
    std::size_t model_top1{};
    std::size_t model_top3{};
    std::size_t model_then_manual_top1{};
    std::size_t manual_top1{};
    std::size_t root_top1{};
    double uas_sum{};
    double las_sum{};
    parsers::dependency_markov::Diagnostics all_diagnostics;

    for (const auto &target : parsed) {
        if (!is_verified(target) || !target.fixture->gold ||
            target.parser_output.status != "ok" ||
            target.parser_output.tree_nbest.empty()) {
            continue;
        }
        parsers::dependency_markov::Model model{order, options.alpha,
                                                options.backoff_strength,
                                                include_predicate_profiles};
        std::size_t training_phrases{};
        std::size_t training_alternatives{};
        for (const auto &training : parsed) {
            if (training.fixture == nullptr || !training.fixture->gold ||
                training.parser_output.status != "ok" ||
                training.parser_output.tree_nbest.empty()) {
                continue;
            }
            if (options.evaluation_policy == EvaluationPolicy::leave_one_out &&
                (training.fixture->id == target.fixture->id ||
                 training.fixture->text == target.fixture->text)) {
                continue;
            }
            std::map<std::string,
                     std::vector<parsers::dependency_markov::Factor>,
                     std::less<>>
                alternatives;
            for (const auto &candidate : training.parser_output.tree_nbest) {
                if (!candidate.matches_dependency_gold) {
                    continue;
                }
                auto candidate_factors = factors(candidate, projection, order,
                                                 include_predicate_profiles);
                alternatives.emplace(factor_signature(candidate_factors),
                                     std::move(candidate_factors));
            }
            const auto phrase_weight = training_weight(training, options);
            if (alternatives.empty() || phrase_weight == 0.0) {
                continue;
            }
            const auto alternative_weight =
                phrase_weight / static_cast<double>(alternatives.size());
            for (const auto &[signature, candidate_factors] : alternatives) {
                static_cast<void>(signature);
                model.train(candidate_factors, alternative_weight);
                ++training_alternatives;
            }
            ++training_phrases;
        }
        if (training_phrases == 0U) {
            continue;
        }

        std::vector<ScoredCandidate> candidates;
        candidates.reserve(target.parser_output.tree_nbest.size());
        for (const auto &candidate : target.parser_output.tree_nbest) {
            const auto score = model.score(factors(candidate, projection, order,
                                                   include_predicate_profiles));
            all_diagnostics.merge(score.diagnostics);
            candidates.push_back(
                ScoredCandidate{.candidate = &candidate,
                                .score = score.log_score,
                                .diagnostics = score.diagnostics});
        }
        auto model_only = candidates;
        std::ranges::stable_sort(
            model_only, [](const auto &left, const auto &right) {
                if (left.score > right.score) {
                    return left.score > right.score;
                }
                if (right.score > left.score) {
                    return false;
                }
                return left.candidate->tree_id < right.candidate->tree_id;
            });
        auto model_then_manual = candidates;
        std::ranges::stable_sort(model_then_manual, [](const auto &left,
                                                       const auto &right) {
            if (left.score > right.score) {
                return left.score > right.score;
            }
            if (right.score > left.score) {
                return false;
            }
            if (left.candidate->manual_score > right.candidate->manual_score) {
                return left.candidate->manual_score >
                       right.candidate->manual_score;
            }
            if (right.candidate->manual_score > left.candidate->manual_score) {
                return false;
            }
            return left.candidate->tree_id < right.candidate->tree_id;
        });
        auto manual = candidates;
        std::ranges::stable_sort(manual, [](const auto &left,
                                            const auto &right) {
            if (left.candidate->manual_score > right.candidate->manual_score) {
                return left.candidate->manual_score >
                       right.candidate->manual_score;
            }
            if (right.candidate->manual_score > left.candidate->manual_score) {
                return false;
            }
            return left.candidate->tree_id < right.candidate->tree_id;
        });

        const auto model_rank = gold_rank(model_only);
        const auto combined_rank = gold_rank(model_then_manual);
        const auto manual_rank = gold_rank(manual);
        const bool gold_available =
            std::ranges::any_of(candidates, [](const auto &candidate) {
                return candidate.candidate->matches_dependency_gold;
            });
        const auto attachment =
            attachment_metrics(*model_only.front().candidate, *target.fixture);
        ++evaluated;
        candidate_gold_recall += static_cast<std::size_t>(gold_available);
        model_top1 += static_cast<std::size_t>(model_rank == 1U);
        model_top3 += static_cast<std::size_t>(model_rank && *model_rank <= 3U);
        model_then_manual_top1 += static_cast<std::size_t>(combined_rank == 1U);
        manual_top1 += static_cast<std::size_t>(manual_rank == 1U);
        root_top1 += static_cast<std::size_t>(attachment.root_correct);
        uas_sum += attachment.uas;
        las_sum += attachment.las;

        fixtures.push_back(
            {{"fixtureId", target.fixture->id},
             {"candidateTrees", candidates.size()},
             {"goldTreeAvailable", gold_available},
             {"trainingPhrases", training_phrases},
             {"trainingAlternatives", training_alternatives},
             {"modelOnly",
              {{"treeId", model_only.front().candidate->tree_id},
               {"score", model_only.front().score},
               {"goldRank", model_rank ? Json(*model_rank) : Json(nullptr)},
               {"rootCorrect", attachment.root_correct},
               {"uas", attachment.uas},
               {"las", attachment.las},
               {"diagnostics",
                diagnostics_json(model_only.front().diagnostics)}}},
             {"modelThenManual",
              {{"treeId", model_then_manual.front().candidate->tree_id},
               {"goldRank",
                combined_rank ? Json(*combined_rank) : Json(nullptr)}}},
             {"manualOnly",
              {{"treeId", manual.front().candidate->tree_id},
               {"goldRank",
                manual_rank ? Json(*manual_rank) : Json(nullptr)}}}});
    }

    const auto denominator = static_cast<double>(evaluated);
    return {
        {"order", order},
        {"projection", projection_name(projection)},
        {"predicateProfiles", include_predicate_profiles},
        {"evaluatedFixtures", evaluated},
        {"candidateGoldRecall", candidate_gold_recall},
        {"modelOnly",
         {{"exactTop1", model_top1},
          {"exactTop3", model_top3},
          {"rootTop1", root_top1},
          {"meanUas", evaluated == 0U ? 0.0 : uas_sum / denominator},
          {"meanLas", evaluated == 0U ? 0.0 : las_sum / denominator},
          {"tieBreak", "tree-id"}}},
        {"modelThenManual",
         {{"exactTop1", model_then_manual_top1},
          {"tieBreak", "manual-score-then-tree-id"}}},
        {"manualOnly", {{"exactTop1", manual_top1}}},
        {"scoringDiagnostics", diagnostics_json(all_diagnostics)},
        {"fixtures", std::move(fixtures)},
    };
}

} // namespace

int main(const int argc, char *argv[]) try {
    const auto options = parse_options(argc, argv);
    if (!options) {
        std::cerr << "dependency_markov_ranker: " << options.error() << '\n';
        return 2;
    }
    auto engine =
        words::Engine::create(read_database(options->database),
                              words::EngineConfig{options->dataset_id});
    if (!engine) {
        std::cerr << "dependency_markov_ranker: " << engine.error().code << ": "
                  << engine.error().message << '\n';
        return 3;
    }
    const auto fixtures = parsers::load_corpus(options->corpus);
    const parsers::Experiment experiment{**engine, options->max_product};
    std::vector<ParsedFixture> parsed;
    parsed.reserve(fixtures.size());
    for (const auto &fixture : fixtures) {
        parsed.push_back(ParsedFixture{
            .fixture = &fixture,
            .parser_output = experiment.run(
                fixture, parsers::Strategy::dependency_tree_oracle),
        });
    }

    Json configurations = Json::array();
    for (const auto projection :
         std::array{Projection::part, Projection::morphology}) {
        for (std::size_t order = 1U; order <= 2U; ++order) {
            for (const auto include_predicate_profiles : {false, true}) {
                configurations.push_back(evaluate(parsed, *options, projection,
                                                  order,
                                                  include_predicate_profiles));
            }
        }
    }
    const Json output{
        {"schema", "words-parser-dependency-markov-investigation"},
        {"schemaVersion", 1},
        {"datasetId", options->dataset_id},
        {"sourceCommit", PARSERS_INVESTIGATION_GIT_COMMIT},
        {"compiler", PARSERS_INVESTIGATION_COMPILER},
        {"compilerVersion", PARSERS_INVESTIGATION_COMPILER_VERSION},
        {"buildType", PARSERS_INVESTIGATION_BUILD_TYPE},
        {"parserStrategy", "dependency-tree-oracle"},
        {"candidatePolicy", "rerank complete trees emitted by the parser"},
        {"trainingPolicy",
         "dependency-gold trees only; phrase weight divided among distinct "
         "factorizations"},
        {"evaluationPolicy",
         options->evaluation_policy == EvaluationPolicy::leave_one_out
             ? "leave-one-fixture-out"
             : "in-sample"},
        {"evaluationTier", "verified-didactic"},
        {"rootConvention",
         "parser finite-verb root; artificial ROOT is only a context marker"},
        {"factorization", "one root factor plus one labeled head-dependent "
                          "factor per non-root token"},
        {"predicateProfileFactor",
         "optional joint multiset of every direct labeled dependent for each "
         "verb; multiplicities preserved"},
        {"orderOneContext", "local head state"},
        {"orderTwoContext",
         "grandparent state, incoming head relation, and local head state"},
        {"trainingWeights",
         {{"verifiedDidactic", 1.0},
          {"syntheticGold", options->synthetic_weight}}},
        {"alpha", options->alpha},
        {"backoffStrength", options->backoff_strength},
        {"maxProduct", options->max_product},
        {"configurations", std::move(configurations)},
    };
    std::cout << output.dump(2) << '\n';
    return 0;
} catch (const std::exception &error) {
    std::cerr << "dependency_markov_ranker: " << error.what() << '\n';
    return 4;
}
