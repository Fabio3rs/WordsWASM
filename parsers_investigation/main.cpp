#include "words/engine.hpp"
#include "words/lexeme.hpp"
#include "words/semantics.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <exception>
#include <filesystem>
#include <fstream>
#include <limits>
#include <print>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#ifndef PARSERS_INVESTIGATION_WWDB_PATH
#error "PARSERS_INVESTIGATION_WWDB_PATH must point to a full WWDB image"
#endif

#ifndef PARSERS_INVESTIGATION_DATASET_ID
#error "PARSERS_INVESTIGATION_DATASET_ID must identify the WWDB dataset"
#endif

namespace {

constexpr std::string_view quotation{"Arma virumque cano"};
constexpr std::array<std::string_view, 3> tokens{"Arma", "virumque", "cano"};

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

void print_field(const std::string_view name, const std::string_view value) {
    if (!value.empty()) {
        std::print(" {}={}", name, value);
    }
}

[[nodiscard]] constexpr std::string_view
person_name(const words::Person person) noexcept {
    switch (std::to_underlying(person)) {
    case std::to_underlying(words::Person::first):
        return "first";
    case std::to_underlying(words::Person::second):
        return "second";
    case std::to_underlying(words::Person::third):
        return "third";
    case std::to_underlying(words::Person::unknown):
        return {};
    default:
        return {};
    }
}

void print_morphology(const words::Morphology &morphology) {
    std::visit(
        []<typename T>(const T &value) {
            using Morphology = std::remove_cvref_t<T>;
            if constexpr (std::is_same_v<Morphology, words::NounMorphology> ||
                          std::is_same_v<Morphology,
                                         words::PronounMorphology> ||
                          std::is_same_v<Morphology,
                                         words::AdjectiveMorphology> ||
                          std::is_same_v<Morphology,
                                         words::NumeralMorphology>) {
                print_field("case", words::case_name(value.grammatical_case));
                print_field("number", words::number_name(value.number));
                print_field("gender", words::gender_name(value.gender));
                if constexpr (std::is_same_v<Morphology,
                                             words::AdjectiveMorphology>) {
                    print_field("degree", words::degree_name(value.degree));
                }
            } else if constexpr (std::is_same_v<Morphology,
                                                words::VerbMorphology>) {
                print_field("tense", words::tense_name(value.tense));
                print_field("voice", words::voice_name(value.voice));
                print_field("mood", words::mood_name(value.mood));
                print_field("person", person_name(value.person));
                print_field("number", words::number_name(value.number));
            } else if constexpr (std::is_same_v<Morphology,
                                                words::ParticipleMorphology>) {
                print_field("case", words::case_name(value.grammatical_case));
                print_field("number", words::number_name(value.number));
                print_field("gender", words::gender_name(value.gender));
                print_field("tense", words::tense_name(value.tense));
                print_field("voice", words::voice_name(value.voice));
            } else if constexpr (std::is_same_v<Morphology,
                                                words::SupineMorphology>) {
                print_field("case", words::case_name(value.grammatical_case));
                print_field("number", words::number_name(value.number));
                print_field("gender", words::gender_name(value.gender));
            } else if constexpr (std::is_same_v<Morphology,
                                                words::AdverbMorphology>) {
                print_field("degree", words::degree_name(value.degree));
            } else if constexpr (std::is_same_v<Morphology,
                                                words::PrepositionMorphology>) {
                print_field("governs", words::case_name(value.governs));
            }
        },
        morphology);
}

void print_derivation(const words::Database &database,
                      const words::DerivationIR &derivation) {
    for (const auto addon_id : derivation.steps()) {
        const auto kind = database.addon_kind(addon_id);
        std::string_view text;
        if (kind == words::AddonKind::prefix ||
            kind == words::AddonKind::tickon) {
            const auto &prefix = database.prefix(addon_id);
            text = database.prefix_string(prefix.fix);
        } else if (kind == words::AddonKind::suffix) {
            const auto &suffix = database.suffix(addon_id);
            text = database.suffix_string(suffix.fix);
        } else if (kind == words::AddonKind::tackon ||
                   kind == words::AddonKind::packon) {
            const auto &tackon = database.tackon(addon_id);
            text = database.tackon_string(tackon.fix);
        }
        std::print(" derivation={}({})", words::addon_kind_name(kind), text);
    }
}

void print_analysis(const words::Engine &engine, const std::string_view token) {
    const auto result = engine.analyze(token);
    std::println("\n[{}] status={} analyses={}", token,
                 words::status_name(result.status), result.analyses.size());

    const auto &database = engine.database();
    for (std::size_t index = 0; index < result.analyses.size(); ++index) {
        const auto &analysis = result.analyses[index];
        const auto &lexeme = database.lexeme(analysis.lexeme);
        std::print("  {}. lemma={} part={}", index + 1U,
                   words::citation_lemma(database, lexeme, token),
                   words::lexical_part_name(lexeme.part_of_speech));
        print_morphology(analysis.morphology);
        std::print(" stem={} ending={}", result.surface.slice(analysis.stem),
                   result.surface.slice(analysis.ending));
        print_derivation(database, analysis.derivation);
        std::println(
            "\n     meaning={}",
            words::normalized_meaning(database.meaning(lexeme.meaning)));
    }

    for (const auto &diagnostic : result.diagnostics) {
        std::println("  diagnostic={} severity={}", diagnostic.code,
                     diagnostic.severity);
    }
}

} // namespace

int main(const int argc, char *argv[]) try {
    if (argc > 2) {
        std::println(stderr, "usage: parsers_investigation [FULL_WWDB]");
        return 2;
    }

    const std::filesystem::path database_path =
        argc == 2 ? argv[1] : PARSERS_INVESTIGATION_WWDB_PATH;
    auto engine = words::Engine::create(
        read_database(database_path),
        words::EngineConfig{PARSERS_INVESTIGATION_DATASET_ID});
    if (!engine) {
        std::println(stderr, "parsers_investigation: {}: {}",
                     engine.error().code, engine.error().message);
        return 3;
    }
    if (!(*engine)->supports_full_analysis()) {
        std::println(stderr, "parsers_investigation: a full WWDB with meanings "
                             "is required");
        return 3;
    }

    std::println("Hello from WordsWASM parsers investigation!");
    std::println("WWDB: {}", database_path.string());
    std::println("Quotation: {}", quotation);
    for (const auto token : tokens) {
        print_analysis(**engine, token);
    }
    return 0;
} catch (const std::exception &error) {
    std::println(stderr, "parsers_investigation: {}", error.what());
    return 4;
}
