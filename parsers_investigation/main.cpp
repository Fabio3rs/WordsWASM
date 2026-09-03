#include "parser.hpp"

#include "words/engine.hpp"

#include <array>
#include <charconv>
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
    std::uint64_t max_product{1'000'000U};
    bool fragment{};
    bool self_test{};
    bool human{};
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
        } else if (argument == "--fragment") {
            options.fragment = true;
        } else if (argument == "--self-test") {
            options.self_test = true;
        } else if (argument == "--human") {
            options.human = true;
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
    return options;
}

void usage(std::ostream &output) {
    output << "usage: parsers_investigation [OPTIONS]\n"
              "  --strategy "
              "morphology|cartesian-leaf-check|incremental-dfs|"
              "dfs-mrv-forward-checking|worklist-prefilter|"
              "gac-propagation|gac-residue-cache|"
              "dependency-projection|dependency-attachment-search|"
              "dependency-tree-oracle|dependency-eisner|dependency-mst|"
              "earley-fixed-point-recognizer|"
              "gslr-stackset-recognizer|all\n"
              "  --text 'Latin text'       parse one text (default: built-in "
              "corpus)\n"
              "  --fragment                allow a verbless --text\n"
              "  --corpus FILE             use a v2 JSON or legacy TSV corpus\n"
              "  --database FILE           load another full or search WWDB\n"
              "  --dataset-id ID           dataset identifier for the WWDB\n"
              "  --max-product N           exact-enumeration safety budget\n"
              "  --human                   compact table instead of NDJSON\n"
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
              << result.elapsed_ns / 1'000U << '\n';
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
    const parsers::Experiment experiment{**engine, options->max_product};
    auto fixtures = parsers::load_corpus(options->corpus);
    if (options->text) {
        fixtures = {parsers::Fixture{
            "ad-hoc",
            *options->text,
            "ad-hoc",
            {},
            options->fragment ? parsers::GrammarMode::fragment
                              : parsers::GrammarMode::complete_clause,
            {},
            std::nullopt,
            std::nullopt}};
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
            } else {
                std::cout << parsers::to_json(result) << '\n';
            }
        }
    }
    return 0;
} catch (const std::exception &error) {
    std::cerr << "parsers_investigation: " << error.what() << '\n';
    return 4;
}
