#include "parser.hpp"

#include "words/engine.hpp"

#include <array>
#include <charconv>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <expected>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#ifndef PARSERS_INVESTIGATION_WWDB_PATH
#error "PARSERS_INVESTIGATION_WWDB_PATH must point to a WWDB image"
#endif

#ifndef PARSERS_INVESTIGATION_DATASET_ID
#error "PARSERS_INVESTIGATION_DATASET_ID must identify the WWDB dataset"
#endif

#ifndef PARSERS_INVESTIGATION_CORPUS_PATH
#error "PARSERS_INVESTIGATION_CORPUS_PATH must point to the common corpus"
#endif

namespace {

struct Options final {
    std::filesystem::path database{PARSERS_INVESTIGATION_WWDB_PATH};
    std::filesystem::path corpus{PARSERS_INVESTIGATION_CORPUS_PATH};
    std::string dataset_id{PARSERS_INVESTIGATION_DATASET_ID};
    std::optional<std::string> text;
    std::optional<parsers::Strategy> strategy;
    bool corpus_explicit{};
    bool all_strategies{};
    std::uint64_t max_product{1'000'000U};
    words::AnalysisOptions analysis_options{};
    bool fragment{};
    bool self_test{};
    bool human{};
    bool json{};
    bool include_nbest{};
    bool include_rejections{};
    bool help{};
};

[[nodiscard]] std::expected<std::uint64_t, std::string>
parse_unsigned(const std::string_view value) {
    std::uint64_t parsed{};
    const auto [end, error] =
        std::from_chars(value.data(), value.data() + value.size(), parsed);
    if (error != std::errc{} || end != value.data() + value.size() ||
        parsed == 0U) {
        return std::unexpected("expected a positive integer: " +
                               std::string{value});
    }
    return parsed;
}

[[nodiscard]] std::expected<Options, std::string>
parse_options(const int argc, char *const argv[]) {
    Options options;
    for (int index = 1; index < argc; ++index) {
        const std::string_view argument{argv[index]};
        const auto require_value =
            [&]() -> std::expected<std::string_view, std::string> {
            if (index + 1 >= argc) {
                return std::unexpected("missing value after " +
                                       std::string{argument});
            }
            ++index;
            return std::string_view{argv[index]};
        };
        if (argument == "--database") {
            auto value = require_value();
            if (!value) {
                return std::unexpected(std::move(value.error()));
            }
            options.database = *value;
        } else if (argument == "--dataset-id") {
            auto value = require_value();
            if (!value) {
                return std::unexpected(std::move(value.error()));
            }
            options.dataset_id = *value;
        } else if (argument == "--corpus") {
            auto value = require_value();
            if (!value) {
                return std::unexpected(std::move(value.error()));
            }
            options.corpus = *value;
            options.corpus_explicit = true;
        } else if (argument == "--text") {
            auto value = require_value();
            if (!value) {
                return std::unexpected(std::move(value.error()));
            }
            options.text = std::string{*value};
        } else if (argument == "--strategy") {
            auto value = require_value();
            if (!value) {
                return std::unexpected(std::move(value.error()));
            }
            if (*value != "all") {
                options.strategy = parsers::parse_strategy(*value);
                if (!options.strategy) {
                    return std::unexpected("unknown strategy: " +
                                           std::string{*value});
                }
            } else {
                options.all_strategies = true;
            }
        } else if (argument == "--max-product") {
            auto value = require_value();
            if (!value) {
                return std::unexpected(std::move(value.error()));
            }
            auto parsed = parse_unsigned(*value);
            if (!parsed) {
                return std::unexpected(std::move(parsed.error()));
            }
            options.max_product = *parsed;
        } else if (argument == "--orthography") {
            auto value = require_value();
            if (!value) {
                return std::unexpected(std::move(value.error()));
            }
            if (*value == "disabled") {
                options.analysis_options.orthography =
                    words::OrthographyMode::disabled;
            } else if (*value == "classical-only") {
                options.analysis_options.orthography =
                    words::OrthographyMode::classical_only;
            } else if (*value == "classical-and-medieval") {
                options.analysis_options.orthography =
                    words::OrthographyMode::classical_and_medieval;
            } else {
                return std::unexpected(
                    "--orthography expects disabled, classical-only, or "
                    "classical-and-medieval");
            }
        } else if (argument == "--disable-mechanism") {
            auto value = require_value();
            if (!value) {
                return std::unexpected(std::move(value.error()));
            }
            auto &mechanisms = options.analysis_options.mechanisms;
            if (*value == "productive-derivations") {
                mechanisms.productive_derivations = false;
            } else if (*value == "prefixes") {
                mechanisms.prefixes = false;
            } else if (*value == "suffixes") {
                mechanisms.suffixes = false;
            } else if (*value == "tickons") {
                mechanisms.tickons = false;
            } else if (*value == "tackons") {
                mechanisms.tackons = false;
            } else if (*value == "packons") {
                mechanisms.packons = false;
            } else if (*value == "syncope") {
                mechanisms.syncope = false;
            } else if (*value == "verbal-compounds") {
                mechanisms.verbal_compounds = false;
            } else {
                return std::unexpected("unknown morphological mechanism: " +
                                       std::string{*value});
            }
        } else if (argument == "--fragment") {
            options.fragment = true;
        } else if (argument == "--self-test") {
            options.self_test = true;
        } else if (argument == "--human") {
            options.human = true;
        } else if (argument == "--json") {
            options.json = true;
        } else if (argument == "--include-nbest") {
            options.include_nbest = true;
        } else if (argument == "--include-rejections") {
            options.include_rejections = true;
        } else if (argument == "--help" || argument == "-h") {
            options.help = true;
        } else {
            return std::unexpected("unknown option: " + std::string{argument});
        }
    }
    if (options.text && options.self_test) {
        return std::unexpected("--text and --self-test cannot be combined");
    }
    if (options.fragment && !options.text) {
        return std::unexpected("--fragment is only valid with --text");
    }
    if (options.json && options.human) {
        return std::unexpected("--json and --human cannot be combined");
    }
    if (options.include_rejections && (!options.text || !options.json)) {
        return std::unexpected(
            "--include-rejections requires --text and --json");
    }
    if (options.text && !options.strategy && !options.all_strategies) {
        options.strategy = parsers::Strategy::dependency_mst;
    }
    return options;
}

void usage(std::ostream &output) {
    output
        << "usage: parsers_investigation [OPTIONS]\n"
           "  --strategy "
           "morphology|cartesian-leaf-check|incremental-dfs|"
           "dfs-mrv-forward-checking|worklist-prefilter|"
           "gac-propagation|gac-residue-cache|"
           "dependency-projection|dependency-attachment-search|"
           "dependency-tree-oracle|dependency-eisner|dependency-mst|"
           "earley-fixed-point-recognizer|"
           "gslr-stackset-recognizer|all\n"
           "  --text 'Latin text'       inspect one text with dependency-mst\n"
           "  --fragment                allow a verbless --text\n"
           "  --corpus FILE             use a v2 JSON or legacy TSV corpus; "
           "with --text, find an exact matching fixture for gold\n"
           "  --database FILE           load another full or search WWDB\n"
           "  --dataset-id ID           dataset identifier for the WWDB\n"
           "  --max-product N           exact-enumeration safety budget\n"
           "  --orthography MODE        disabled, classical-only, or "
           "classical-and-medieval (default)\n"
           "  --disable-mechanism NAME  disable one core mechanism; may "
           "repeat\n"
           "  --human                   compact table instead of NDJSON\n"
           "  --json                    NDJSON output for --text\n"
           "  --include-nbest           include every possible analysis "
           "in NDJSON\n"
           "  --include-rejections      include rejected partial analyses "
           "in --text --json\n"
           "  --self-test               verify strategy invariants on the "
           "corpus\n";
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

void print_human_header() {
    std::cout << "fixture\tstrategy\tstatus\traw-product\tpruned-product\t"
                 "assignments\tparser-units\tduplicates\tus\n";
}

void print_human(const parsers::Result &result) {
    std::cout << result.fixture_id << '\t'
              << parsers::strategy_name(result.strategy) << '\t'
              << result.status << '\t' << result.raw_product << '\t'
              << (result.pruned_product.empty() ? "-" : result.pruned_product)
              << '\t' << result.accepted_assignments << '\t'
              << result.parser_units_created << '\t'
              << result.parser_duplicate_deductions << '\t'
              << std::chrono::duration_cast<std::chrono::microseconds>(
                     std::chrono::nanoseconds{result.elapsed_ns})
                     .count()
              << '\n';
}

void print_inspection(const parsers::Result &result) {
    std::cout << result.text << "\n"
              << "Estratégia: " << parsers::strategy_name(result.strategy)
              << " | estado: " << result.status;
    if (result.strategy != parsers::Strategy::morphology) {
        std::cout << " | análises aceitas: " << result.accepted_assignments;
    }
    if (result.best_score) {
        std::cout << " | score: " << *result.best_score;
    }
    std::cout << "\n";
    if (!result.best_analysis.empty()) {
        std::cout << "\nMelhor análise:\n";
        for (const auto &choice : result.best_analysis) {
            const auto surface = choice.token < result.surface_tokens.size()
                                     ? result.surface_tokens[choice.token]
                                     : "?";
            std::cout << "  " << choice.token + 1U << ". " << surface << " → "
                      << choice.lemma << " (" << choice.part;
            if (!choice.morphology.empty()) {
                std::cout << ", " << choice.morphology;
            }
            std::cout << ")";
            if (choice.token < result.candidate_counts.size()) {
                std::cout << " [candidato " << choice.candidate + 1U << '/'
                          << result.candidate_counts[choice.token] << ']';
            }
            std::cout << '\n';
        }
    } else {
        std::cout << "\nNenhuma análise selecionada.\n";
        if (result.strategy == parsers::Strategy::morphology) {
            std::cout << "Candidatos morfológicos por token:\n";
            for (std::size_t token = 0; token < result.surface_tokens.size();
                 ++token) {
                std::cout << "  " << token + 1U << ". "
                          << result.surface_tokens[token] << ": "
                          << result.candidate_counts[token] << '\n';
            }
        }
    }
    if (!result.best_relations.empty()) {
        std::cout << "\nDependências:\n";
        for (const auto &relation : result.best_relations) {
            const auto dependent =
                relation.dependent < result.surface_tokens.size()
                    ? result.surface_tokens[relation.dependent]
                    : "?";
            std::cout << "  " << relation.dependent + 1U << ':' << dependent
                      << " → ";
            if (relation.head &&
                *relation.head < result.surface_tokens.size()) {
                std::cout << *relation.head + 1U << ':'
                          << result.surface_tokens[*relation.head];
            } else {
                std::cout << "ROOT";
            }
            std::cout << " (" << relation.label << ")\n";
        }
    }
    if (result.morphology_gold_declared || result.dependency_gold_declared) {
        std::cout << "\nGold da fixture " << result.fixture_id << ":\n";
        if (result.morphology_gold_declared) {
            std::cout << "  Morfologia: ";
            if (result.strategy == parsers::Strategy::morphology) {
                std::cout << (result.morphology_gold_in_lattice
                                  ? "presente no lattice"
                                  : "ausente do lattice");
            } else {
                std::cout << (result.morphology_gold_survives ? "preservada"
                                                              : "ausente");
            }
            if (result.morphology_gold_rank) {
                std::cout << " (rank " << *result.morphology_gold_rank << ')';
            }
            std::cout << '\n';
        }
        if (result.dependency_gold_declared) {
            std::cout << "  Dependências: "
                      << (result.dependency_gold_survives
                              ? (*result.dependency_gold_survives
                                     ? "preservadas"
                                     : "ausentes")
                              : "não avaliadas");
            if (result.dependency_gold_rank) {
                std::cout << " (rank " << *result.dependency_gold_rank << ')';
            }
            std::cout << '\n';
        }
    }
    for (const auto &diagnostic : result.diagnostics) {
        std::cout << "  Diagnóstico: " << diagnostic << '\n';
    }
}

} // namespace

int main(const int argc, char *argv[]) try {
    auto options = parse_options(argc, argv);
    if (!options) {
        usage(std::cerr);
        std::cerr << "parsers_investigation: " << options.error() << '\n';
        return 2;
    }
    if (options->help) {
        usage(std::cout);
        return 0;
    }

    auto engine =
        words::Engine::create(read_database(options->database),
                              words::EngineConfig{options->dataset_id});
    if (!engine) {
        std::cerr << "parsers_investigation: " << engine.error().code << ": "
                  << engine.error().message << '\n';
        return 3;
    }
    const parsers::Experiment experiment{**engine, options->max_product,
                                         options->analysis_options,
                                         options->include_rejections};
    std::vector<parsers::Fixture> fixtures;
    if (options->text) {
        if (options->corpus_explicit) {
            auto corpus = parsers::load_corpus(options->corpus);
            for (auto &fixture : corpus) {
                if (fixture.text == *options->text) {
                    fixtures.push_back(std::move(fixture));
                }
            }
            if (fixtures.size() != 1U) {
                std::cerr << "parsers_investigation: --text must match exactly "
                             "one fixture in --corpus (found "
                          << fixtures.size() << ")\n";
                return 2;
            }
            if (options->fragment) {
                fixtures.front().mode = parsers::GrammarMode::fragment;
            }
        } else {
            fixtures = {parsers::Fixture{
                .id = "ad-hoc",
                .text = *options->text,
                .phenomenon = "ad-hoc",
                .preferred_lemmas = {},
                .mode = options->fragment
                            ? parsers::GrammarMode::fragment
                            : parsers::GrammarMode::complete_clause,
                .lookup_overrides = {},
                .annotation = std::nullopt,
                .gold = std::nullopt}};
        }
    } else {
        fixtures = parsers::load_corpus(options->corpus);
    }

    if (options->self_test) {
        std::string failure;
        if (!experiment.self_test(fixtures, failure)) {
            std::cerr << "parsers_investigation: self-test failed: " << failure
                      << '\n';
            return 1;
        }
        std::cout << "self-test: " << fixtures.size() << " fixtures passed\n";
        return 0;
    }

    constexpr std::array all_strategies{
        parsers::Strategy::morphology,
        parsers::Strategy::cartesian_leaf_check,
        parsers::Strategy::incremental_dfs,
        parsers::Strategy::dfs_mrv_forward_checking,
        parsers::Strategy::worklist_prefilter,
        parsers::Strategy::gac_propagation,
        parsers::Strategy::gac_residue_cache,
        parsers::Strategy::dependency_projection,
        parsers::Strategy::dependency_attachment_search,
        parsers::Strategy::dependency_tree_oracle,
        parsers::Strategy::dependency_eisner,
        parsers::Strategy::dependency_mst,
        parsers::Strategy::earley_fixed_point_recognizer,
        parsers::Strategy::gslr_stackset_recognizer};
    const bool inspect = options->text && !options->json && !options->human;
    if (options->human) {
        print_human_header();
    }
    for (const auto &fixture : fixtures) {
        for (const auto strategy : all_strategies) {
            if (options->strategy && *options->strategy != strategy) {
                continue;
            }
            const auto result = experiment.run(fixture, strategy);
            if (options->human) {
                print_human(result);
            } else if (inspect) {
                print_inspection(result);
                if (options->all_strategies) {
                    std::cout << '\n';
                }
            } else {
                std::cout << parsers::to_json(result, options->include_nbest,
                                               options->include_rejections)
                          << '\n';
            }
        }
    }
    return 0;
} catch (const std::exception &error) {
    std::cerr << "parsers_investigation: " << error.what() << '\n';
    return 4;
}
