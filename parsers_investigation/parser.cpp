#include "parser.hpp"

#include "words/lexeme.hpp"
#include "words/semantics.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <bitset>
#include <boost/multiprecision/cpp_int.hpp>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <functional>
#include <iomanip>
#include <limits>
#include <map>
#include <numeric>
#include <optional>
#include <queue>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

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

namespace parsers {
namespace {

using boost::multiprecision::cpp_int;
using Clock = std::chrono::steady_clock;

enum class Category : std::uint8_t {
    finite,
    nominal,
    modifier,
    preposition,
    conjunction,
    adverb,
    nonfinite,
    other,
};

constexpr std::size_t category_count{8U};
constexpr int first_nonterminal{100};
constexpr int augmented_symbol{100};
constexpr int sentence_symbol{101};
constexpr int sequence_symbol{102};
constexpr int unit_symbol{103};
constexpr int eof_symbol{8};

enum class Compatibility : std::uint8_t {
    compatible,
    incompatible,
    indeterminate,
};

struct Candidate final {
    std::size_t source_index{};
    std::string lemma;
    words::PartOfSpeech part{words::PartOfSpeech::unknown};
    words::Morphology morphology;
    words::VerbKind verb_kind{words::VerbKind::unknown};
    words::LexicalFrequency lexical_frequency{words::LexicalFrequency::unknown};
    words::RuleFrequency rule_frequency{words::RuleFrequency::unknown};
    bool has_rule{};
    bool enclitic_que{};
};

struct Lattice final {
    std::vector<Token> tokens;
    std::vector<std::vector<Candidate>> candidates;
};

struct CandidateRef final {
    std::size_t token{};
    std::size_t candidate{};
    bool operator==(const CandidateRef &) const = default;
};

enum class RelationCandidateKind : std::uint8_t {
    preposition_complement,
    verb_argument,
    coordination,
    comparison_standard,
};

struct RelationCandidate final {
    RelationCandidateKind kind{RelationCandidateKind::preposition_complement};
    CandidateRef governor;
    CandidateRef dependent;
    std::vector<CandidateRef> contexts;
    std::string_view constraint_id;
    Compatibility compatibility{Compatibility::indeterminate};
};

struct RelationLattice final {
    std::vector<RelationCandidate> candidates;
    std::map<std::string, std::uint64_t, std::less<>> by_kind;
    std::map<std::string, std::uint64_t, std::less<>> by_compatibility;
};

using Assignment = std::vector<std::size_t>;
using Domains = std::vector<std::vector<bool>>;

struct Enumeration final {
    std::vector<Assignment> valid;
    std::map<std::string, std::uint64_t, std::less<>> rejections;
    std::uint64_t checks{};
    std::uint64_t states{};
    std::uint64_t backtracks{};
    std::uint64_t complete{};
};

struct NominalFeatures final {
    words::GrammaticalCase grammatical_case{words::GrammaticalCase::unknown};
    words::GrammaticalNumber number{words::GrammaticalNumber::unknown};
    words::Gender gender{words::Gender::unknown};
};

[[nodiscard]] bool ascii_separator(const unsigned char value) noexcept {
    if (value >= 0x80U) {
        return false;
    }
    return std::isspace(static_cast<int>(value)) != 0 ||
           std::string_view{",.;:!?()[]{}\"'/-"}.find(
               static_cast<char>(value)) != std::string_view::npos;
}

[[nodiscard]] std::vector<std::string> split(const std::string_view value,
                                             const char delimiter) {
    std::vector<std::string> result;
    std::size_t begin{};
    while (begin <= value.size()) {
        const auto end = value.find(delimiter, begin);
        if (end == std::string_view::npos) {
            result.emplace_back(value.substr(begin));
            break;
        }
        result.emplace_back(value.substr(begin, end - begin));
        begin = end + 1U;
    }
    return result;
}

[[nodiscard]] bool is_finite(const Candidate &candidate) noexcept {
    const auto *verb =
        std::get_if<words::VerbMorphology>(&candidate.morphology);
    return verb != nullptr && verb->mood != words::Mood::unknown &&
           verb->mood != words::Mood::infinitive &&
           verb->mood != words::Mood::participle;
}

[[nodiscard]] words::PartOfSpeech
surface_part(const words::Morphology &morphology,
             const words::PartOfSpeech lexical_part) noexcept {
    return std::visit(
        [lexical_part]<typename T>(const T &) {
            using Morphology = std::remove_cvref_t<T>;
            if constexpr (std::is_same_v<Morphology, words::NounMorphology>) {
                return words::PartOfSpeech::noun;
            } else if constexpr (std::is_same_v<Morphology,
                                                words::PronounMorphology>) {
                return lexical_part == words::PartOfSpeech::pack
                           ? lexical_part
                           : words::PartOfSpeech::pronoun;
            } else if constexpr (std::is_same_v<Morphology,
                                                words::AdjectiveMorphology>) {
                return words::PartOfSpeech::adjective;
            } else if constexpr (std::is_same_v<Morphology,
                                                words::NumeralMorphology>) {
                return words::PartOfSpeech::numeral;
            } else if constexpr (std::is_same_v<Morphology,
                                                words::AdverbMorphology>) {
                return words::PartOfSpeech::adverb;
            } else if constexpr (std::is_same_v<Morphology,
                                                words::VerbMorphology>) {
                return words::PartOfSpeech::verb;
            } else if constexpr (std::is_same_v<Morphology,
                                                words::ParticipleMorphology>) {
                return words::PartOfSpeech::participle;
            } else if constexpr (std::is_same_v<Morphology,
                                                words::SupineMorphology>) {
                return words::PartOfSpeech::supine;
            } else if constexpr (std::is_same_v<Morphology,
                                                words::PrepositionMorphology>) {
                return words::PartOfSpeech::preposition;
            } else {
                return lexical_part;
            }
        },
        morphology);
}

[[nodiscard]] std::string_view
surface_part_name(const words::PartOfSpeech part) noexcept {
    constexpr std::array<std::string_view, 16> names{
        "unknown",      "noun",    "pronoun",     "pack",
        "adjective",    "numeral", "adverb",      "verb",
        "participle",   "supine",  "preposition", "conjunction",
        "interjection", "tackon",  "prefix",      "suffix",
    };
    const auto index = static_cast<std::size_t>(std::to_underlying(part));
    return index < names.size() ? names[index] : names.front();
}

[[nodiscard]] std::optional<NominalFeatures>
nominal_features(const Candidate &candidate) noexcept {
    return std::visit(
        []<typename T>(const T &value) -> std::optional<NominalFeatures> {
            using Morphology = std::remove_cvref_t<T>;
            if constexpr (
                std::is_same_v<Morphology, words::NounMorphology> ||
                std::is_same_v<Morphology, words::PronounMorphology> ||
                std::is_same_v<Morphology, words::AdjectiveMorphology> ||
                std::is_same_v<Morphology, words::NumeralMorphology> ||
                std::is_same_v<Morphology, words::ParticipleMorphology> ||
                std::is_same_v<Morphology, words::SupineMorphology>) {
                return NominalFeatures{value.grammatical_case, value.number,
                                       value.gender};
            }
            return std::nullopt;
        },
        candidate.morphology);
}

[[nodiscard]] bool is_noun_like(const Candidate &candidate) noexcept {
    return candidate.part == words::PartOfSpeech::noun ||
           candidate.part == words::PartOfSpeech::pronoun ||
           candidate.part == words::PartOfSpeech::pack ||
           candidate.part == words::PartOfSpeech::numeral;
}

[[nodiscard]] bool is_modifier(const Candidate &candidate) noexcept {
    return candidate.part == words::PartOfSpeech::adjective ||
           candidate.part == words::PartOfSpeech::participle;
}

[[nodiscard]] bool
is_comparative_adjective(const Candidate &candidate) noexcept {
    const auto *adjective =
        std::get_if<words::AdjectiveMorphology>(&candidate.morphology);
    return adjective != nullptr &&
           adjective->degree == words::Degree::comparative;
}

[[nodiscard]] Compatibility
compare(const words::GrammaticalCase left,
        const words::GrammaticalCase right) noexcept {
    if (left == words::GrammaticalCase::unknown ||
        right == words::GrammaticalCase::unknown) {
        return Compatibility::indeterminate;
    }
    return left == right ? Compatibility::compatible
                         : Compatibility::incompatible;
}

[[nodiscard]] Compatibility
compare(const words::GrammaticalNumber left,
        const words::GrammaticalNumber right) noexcept {
    if (left == words::GrammaticalNumber::unknown ||
        right == words::GrammaticalNumber::unknown) {
        return Compatibility::indeterminate;
    }
    return left == right ? Compatibility::compatible
                         : Compatibility::incompatible;
}

[[nodiscard]] Compatibility compare_gender(const words::Gender left,
                                           const words::Gender right) noexcept {
    if (left == words::Gender::unknown || right == words::Gender::unknown) {
        return Compatibility::indeterminate;
    }
    return left == right || left == words::Gender::common ||
                   right == words::Gender::common
               ? Compatibility::compatible
               : Compatibility::incompatible;
}

template <typename T> [[nodiscard]] bool has_support(const T result) noexcept {
    return result != Compatibility::incompatible;
}

[[nodiscard]] bool agrees(const Candidate &left,
                          const Candidate &right) noexcept {
    const auto left_features = nominal_features(left);
    const auto right_features = nominal_features(right);
    return left_features && right_features &&
           compare(left_features->grammatical_case,
                   right_features->grammatical_case) ==
               Compatibility::compatible &&
           compare(left_features->number, right_features->number) ==
               Compatibility::compatible &&
           compare_gender(left_features->gender, right_features->gender) ==
               Compatibility::compatible;
}

[[nodiscard]] Category category(const Candidate &candidate) noexcept {
    if (is_finite(candidate)) {
        return Category::finite;
    }
    if (is_noun_like(candidate)) {
        return Category::nominal;
    }
    if (is_modifier(candidate)) {
        return Category::modifier;
    }
    if (candidate.part == words::PartOfSpeech::preposition) {
        return Category::preposition;
    }
    if (candidate.part == words::PartOfSpeech::conjunction ||
        candidate.part == words::PartOfSpeech::tackon) {
        return Category::conjunction;
    }
    if (candidate.part == words::PartOfSpeech::adverb) {
        return Category::adverb;
    }
    if (candidate.part == words::PartOfSpeech::supine) {
        return Category::nonfinite;
    }
    if (const auto *verb =
            std::get_if<words::VerbMorphology>(&candidate.morphology);
        verb != nullptr && verb->mood == words::Mood::infinitive) {
        return Category::nonfinite;
    }
    return Category::other;
}

[[nodiscard]] std::string morphology_name(const Candidate &candidate) {
    return std::visit(
        []<typename T>(const T &value) -> std::string {
            using Morphology = std::remove_cvref_t<T>;
            std::ostringstream output;
            if constexpr (std::is_same_v<Morphology, words::NounMorphology> ||
                          std::is_same_v<Morphology,
                                         words::PronounMorphology> ||
                          std::is_same_v<Morphology,
                                         words::NumeralMorphology>) {
                output << words::case_name(value.grammatical_case) << '-'
                       << words::number_name(value.number) << '-'
                       << words::gender_name(value.gender);
            } else if constexpr (std::is_same_v<
                                     Morphology,
                                     words::AdjectiveMorphology>) {
                output << words::case_name(value.grammatical_case) << '-'
                       << words::number_name(value.number) << '-'
                       << words::gender_name(value.gender) << '-'
                       << words::degree_name(value.degree);
            } else if constexpr (std::is_same_v<Morphology,
                                                words::VerbMorphology>) {
                output << words::tense_name(value.tense) << '-'
                       << words::voice_name(value.voice) << '-'
                       << words::mood_name(value.mood) << '-'
                       << static_cast<unsigned>(
                              std::to_underlying(value.person))
                       << '-' << words::number_name(value.number);
            } else if constexpr (std::is_same_v<Morphology,
                                                words::ParticipleMorphology>) {
                output << words::case_name(value.grammatical_case) << '-'
                       << words::number_name(value.number) << '-'
                       << words::gender_name(value.gender) << '-'
                       << words::tense_name(value.tense) << '-'
                       << words::voice_name(value.voice);
            } else if constexpr (std::is_same_v<Morphology,
                                                words::SupineMorphology>) {
                output << words::case_name(value.grammatical_case) << '-'
                       << words::number_name(value.number) << '-'
                       << words::gender_name(value.gender);
            } else if constexpr (std::is_same_v<Morphology,
                                                words::PrepositionMorphology>) {
                output << "governs-" << words::case_name(value.governs);
            } else if constexpr (std::is_same_v<Morphology,
                                                words::AdverbMorphology>) {
                output << words::degree_name(value.degree);
            } else {
                output << "invariable";
            }
            return output.str();
        },
        candidate.morphology);
}

[[nodiscard]] Lattice build_lattice(const words::Engine &engine,
                                    const std::string_view text,
                                    const std::vector<LookupOverride> &overrides) {
    Lattice lattice;
    lattice.tokens = tokenize(text);
    std::vector<bool> overridden(lattice.tokens.size());
    for (const auto &override : overrides) {
        if (override.token >= lattice.tokens.size() || override.lookup.empty() ||
            override.reason.empty() || overridden[override.token]) {
            throw std::runtime_error{"invalid lookup override at token " +
                                     std::to_string(override.token)};
        }
        overridden[override.token] = true;
        lattice.tokens[override.token].lookup = override.lookup;
    }
    lattice.candidates.reserve(lattice.tokens.size());
    const auto &database = engine.database();
    for (const auto &token : lattice.tokens) {
        const auto query = engine.analyze(token.lookup);
        std::vector<Candidate> candidates;
        candidates.reserve(query.analyses.size());
        for (std::size_t index = 0; index < query.analyses.size(); ++index) {
            const auto &analysis = query.analyses[index];
            const auto &lexeme = database.lexeme(analysis.lexeme);
            bool enclitic_que{};
            for (const auto addon_id : analysis.derivation.steps()) {
                const auto kind = database.addon_kind(addon_id);
                if (kind != words::AddonKind::tackon &&
                    kind != words::AddonKind::packon) {
                    continue;
                }
                const auto &tackon = database.tackon(addon_id);
                if (tackon.enclitic &&
                    database.tackon_string(tackon.fix) == "que") {
                    enclitic_que = true;
                }
            }
            auto rule_frequency = words::RuleFrequency::unknown;
            if (analysis.rule) {
                rule_frequency = database.rule(*analysis.rule).frequency;
            }
            candidates.push_back(Candidate{
                index,
                words::citation_lemma(database, lexeme, token.lookup),
                surface_part(analysis.morphology, lexeme.part_of_speech),
                analysis.morphology,
                lexeme.verb_kind,
                lexeme.frequency,
                rule_frequency,
                analysis.rule.has_value(),
                enclitic_que,
            });
        }
        lattice.candidates.push_back(std::move(candidates));
    }
    return lattice;
}

[[nodiscard]] constexpr std::string_view
relation_kind_name(const RelationCandidateKind kind) noexcept {
    switch (std::to_underlying(kind)) {
    case std::to_underlying(RelationCandidateKind::preposition_complement):
        return "preposition-complement";
    case std::to_underlying(RelationCandidateKind::verb_argument):
        return "verb-argument";
    case std::to_underlying(RelationCandidateKind::coordination):
        return "coordination";
    case std::to_underlying(RelationCandidateKind::comparison_standard):
        return "comparison-standard";
    default:
        return "unknown";
    }
}

[[nodiscard]] constexpr std::string_view
compatibility_name(const Compatibility compatibility) noexcept {
    switch (std::to_underlying(compatibility)) {
    case std::to_underlying(Compatibility::compatible):
        return "compatible";
    case std::to_underlying(Compatibility::incompatible):
        return "incompatible";
    case std::to_underlying(Compatibility::indeterminate):
        return "indeterminate";
    default:
        return "unknown";
    }
}

void add_relation_candidate(RelationLattice &relations,
                            const RelationCandidateKind kind,
                            const CandidateRef governor,
                            const CandidateRef dependent,
                            const std::string_view constraint_id,
                            const Compatibility compatibility,
                            std::vector<CandidateRef> contexts = {}) {
    relations.candidates.push_back(RelationCandidate{
        kind, governor, dependent, std::move(contexts), constraint_id,
        compatibility});
    ++relations.by_kind[std::string{relation_kind_name(kind)}];
    ++relations
          .by_compatibility[std::string{compatibility_name(compatibility)}];
}

[[nodiscard]] RelationLattice build_relation_lattice(const Lattice &lattice) {
    RelationLattice relations;
    for (std::size_t governor_token = 0;
         governor_token < lattice.candidates.size(); ++governor_token) {
        for (std::size_t governor_candidate = 0;
             governor_candidate < lattice.candidates[governor_token].size();
             ++governor_candidate) {
            const auto &governor =
                lattice.candidates[governor_token][governor_candidate];
            const CandidateRef governor_ref{governor_token, governor_candidate};

            if (const auto *preposition =
                    std::get_if<words::PrepositionMorphology>(
                        &governor.morphology)) {
                for (std::size_t dependent_token = governor_token + 1U;
                     dependent_token < lattice.candidates.size();
                     ++dependent_token) {
                    for (std::size_t dependent_candidate = 0;
                         dependent_candidate <
                         lattice.candidates[dependent_token].size();
                         ++dependent_candidate) {
                        const auto features = nominal_features(
                            lattice.candidates[dependent_token]
                                              [dependent_candidate]);
                        if (!features) {
                            continue;
                        }
                        add_relation_candidate(
                            relations,
                            RelationCandidateKind::preposition_complement,
                            governor_ref,
                            CandidateRef{dependent_token, dependent_candidate},
                            "H005",
                            compare(features->grammatical_case,
                                    preposition->governs));
                    }
                }
            }

            if (std::holds_alternative<words::VerbMorphology>(
                    governor.morphology)) {
                const auto required = words::governed_case(governor.verb_kind);
                for (std::size_t dependent_token = 0;
                     dependent_token < lattice.candidates.size();
                     ++dependent_token) {
                    if (dependent_token == governor_token) {
                        continue;
                    }
                    for (std::size_t dependent_candidate = 0;
                         dependent_candidate <
                         lattice.candidates[dependent_token].size();
                         ++dependent_candidate) {
                        const auto &dependent =
                            lattice.candidates[dependent_token]
                                              [dependent_candidate];
                        if (!is_noun_like(dependent)) {
                            continue;
                        }
                        const auto features = nominal_features(dependent);
                        const auto compatibility =
                            required && features
                                ? compare(features->grammatical_case, *required)
                                : Compatibility::indeterminate;
                        add_relation_candidate(
                            relations, RelationCandidateKind::verb_argument,
                            governor_ref,
                            CandidateRef{dependent_token, dependent_candidate},
                            "H006", compatibility);
                    }
                }
            }

            if (is_comparative_adjective(governor)) {
                for (std::size_t dependent_token = governor_token + 1U;
                     dependent_token < lattice.candidates.size();
                     ++dependent_token) {
                    std::vector<CandidateRef> comparative_markers;
                    for (std::size_t marker_token = governor_token + 1U;
                         marker_token < dependent_token; ++marker_token) {
                        if (lattice.tokens[marker_token].lookup != "quam") {
                            continue;
                        }
                        for (std::size_t marker_candidate = 0;
                             marker_candidate <
                             lattice.candidates[marker_token].size();
                             ++marker_candidate) {
                            const auto part =
                                lattice.candidates[marker_token]
                                                  [marker_candidate]
                                                      .part;
                            if (part == words::PartOfSpeech::conjunction ||
                                part == words::PartOfSpeech::adverb) {
                                comparative_markers.push_back(CandidateRef{
                                    marker_token, marker_candidate});
                            }
                        }
                    }
                    for (std::size_t dependent_candidate = 0;
                         dependent_candidate <
                         lattice.candidates[dependent_token].size();
                         ++dependent_candidate) {
                        const auto dependent_features = nominal_features(
                            lattice.candidates[dependent_token]
                                              [dependent_candidate]);
                        if (!dependent_features ||
                            !is_noun_like(lattice.candidates[dependent_token]
                                                           [dependent_candidate])) {
                            continue;
                        }
                        if (comparative_markers.empty()) {
                            add_relation_candidate(
                                relations,
                                RelationCandidateKind::comparison_standard,
                                governor_ref,
                                CandidateRef{dependent_token,
                                             dependent_candidate},
                                "H011",
                                compare(dependent_features->grammatical_case,
                                        words::GrammaticalCase::ablative));
                            continue;
                        }
                        for (const auto marker : comparative_markers) {
                            for (std::size_t context_token = 0;
                                 context_token < governor_token;
                                 ++context_token) {
                                for (std::size_t context_candidate = 0;
                                     context_candidate <
                                     lattice.candidates[context_token].size();
                                     ++context_candidate) {
                                    const auto &context =
                                        lattice.candidates[context_token]
                                                          [context_candidate];
                                    const auto context_features =
                                        nominal_features(context);
                                    if (!context_features ||
                                        !is_noun_like(context)) {
                                        continue;
                                    }
                                    add_relation_candidate(
                                        relations,
                                        RelationCandidateKind::comparison_standard,
                                        governor_ref,
                                        CandidateRef{dependent_token,
                                                     dependent_candidate},
                                        "H011",
                                        compare(context_features
                                                    ->grammatical_case,
                                                dependent_features
                                                    ->grammatical_case),
                                        {CandidateRef{context_token,
                                                      context_candidate},
                                         marker});
                                }
                            }
                        }
                    }
                }
            }

            if (governor.enclitic_que) {
                for (std::size_t head_token = 0; head_token < governor_token;
                     ++head_token) {
                    for (std::size_t head_candidate = 0;
                         head_candidate < lattice.candidates[head_token].size();
                         ++head_candidate) {
                        const auto &head =
                            lattice.candidates[head_token][head_candidate];
                        const auto dependent_features =
                            nominal_features(governor);
                        const auto head_features = nominal_features(head);
                        Compatibility compatibility{
                            Compatibility::incompatible};
                        if (dependent_features && head_features) {
                            compatibility =
                                compare(dependent_features->grammatical_case,
                                        head_features->grammatical_case);
                        } else if (!dependent_features &&
                                   category(governor) == category(head)) {
                            compatibility = Compatibility::compatible;
                        }
                        add_relation_candidate(
                            relations, RelationCandidateKind::coordination,
                            CandidateRef{head_token, head_candidate},
                            governor_ref, "H007", compatibility);
                    }
                }
            }
        }
    }
    return relations;
}

[[nodiscard]] bool relation_endpoint_active(const Domains &domains,
                                            const CandidateRef endpoint) {
    return endpoint.token < domains.size() &&
           endpoint.candidate < domains[endpoint.token].size() &&
           domains[endpoint.token][endpoint.candidate];
}

[[nodiscard]] bool relation_selected(const RelationCandidate &relation,
                                     const Assignment &assignment) {
    return relation.governor.token < assignment.size() &&
           relation.dependent.token < assignment.size() &&
           assignment[relation.governor.token] == relation.governor.candidate &&
           assignment[relation.dependent.token] == relation.dependent.candidate &&
           std::ranges::all_of(
               relation.contexts, [&](const CandidateRef context) {
                   return context.token < assignment.size() &&
                          assignment[context.token] == context.candidate;
               });
}

[[nodiscard]] bool relation_allowed(const RelationCandidate &relation) {
    return relation.compatibility != Compatibility::incompatible;
}

[[nodiscard]] bool
selected_relation_exists(const RelationLattice &relations,
                         const Assignment &assignment,
                         const RelationCandidateKind kind,
                         const std::optional<std::size_t> governor_token,
                         const std::optional<std::size_t> dependent_token) {
    return std::ranges::any_of(
        relations.candidates, [&](const RelationCandidate &relation) {
            return relation.kind == kind && relation_allowed(relation) &&
                   relation_selected(relation, assignment) &&
                   (!governor_token ||
                    relation.governor.token == *governor_token) &&
                   (!dependent_token ||
                    relation.dependent.token == *dependent_token);
        });
}

[[nodiscard]] cpp_int raw_product(const Lattice &lattice) {
    cpp_int product{1};
    for (const auto &domain : lattice.candidates) {
        product *= domain.size();
    }
    if (lattice.candidates.empty()) {
        return 0;
    }
    return product;
}

[[nodiscard]] cpp_int domain_product(const Domains &domains) {
    cpp_int product{1};
    for (const auto &domain : domains) {
        product *= std::ranges::count(domain, true);
    }
    return domains.empty() ? cpp_int{0} : product;
}

[[nodiscard]] std::vector<const Candidate *>
resolve(const Lattice &lattice, const Assignment &assignment) {
    std::vector<const Candidate *> result;
    result.reserve(assignment.size());
    for (std::size_t token = 0; token < assignment.size(); ++token) {
        result.push_back(&lattice.candidates[token][assignment[token]]);
    }
    return result;
}

[[nodiscard]] std::optional<std::string_view>
first_violation(const Lattice &lattice, const RelationLattice &relations,
                const Assignment &assignment, const bool fragment,
                std::uint64_t &checks) {
    const auto choice = resolve(lattice, assignment);
    ++checks;
    if (!fragment &&
        std::ranges::none_of(choice, [](const Candidate *candidate) {
            return is_finite(*candidate);
        })) {
        return "H002";
    }

    // H003 is deliberately conservative: pro-drop is valid, so a nominal is
    // forced to be a subject only when there is exactly one nominative and all
    // finite predicates are third person.
    std::vector<const NominalFeatures *> nominatives;
    std::vector<NominalFeatures> nominal_storage;
    nominal_storage.reserve(choice.size());
    bool all_finite_third{true};
    bool found_finite{};
    std::vector<words::GrammaticalNumber> finite_numbers;
    for (const auto *candidate : choice) {
        if (is_noun_like(*candidate)) {
            const auto features = nominal_features(*candidate);
            if (features && features->grammatical_case ==
                                words::GrammaticalCase::nominative) {
                nominal_storage.push_back(*features);
            }
        }
        if (const auto *verb =
                std::get_if<words::VerbMorphology>(&candidate->morphology);
            verb != nullptr && is_finite(*candidate)) {
            found_finite = true;
            all_finite_third =
                all_finite_third && verb->person == words::Person::third;
            finite_numbers.push_back(verb->number);
        }
    }
    for (const auto &features : nominal_storage) {
        nominatives.push_back(&features);
    }
    ++checks;
    if (found_finite && all_finite_third && nominatives.size() == 1U &&
        std::ranges::none_of(
            finite_numbers, [&](const words::GrammaticalNumber number) {
                return has_support(
                    compare(nominatives.front()->number, number));
            })) {
        return "H003";
    }

    for (std::size_t token = 0; token < choice.size(); ++token) {
        const auto *preposition = std::get_if<words::PrepositionMorphology>(
            &choice[token]->morphology);
        if (preposition != nullptr) {
            ++checks;
            const bool supported = selected_relation_exists(
                relations, assignment,
                RelationCandidateKind::preposition_complement, token,
                std::nullopt);
            if (!supported) {
                return "H005";
            }
        }

        if (choice[token]->enclitic_que) {
            ++checks;
            const bool supported = selected_relation_exists(
                relations, assignment, RelationCandidateKind::coordination,
                std::nullopt, token);
            if (!supported) {
                return "H007";
            }
        }
    }
    return std::nullopt;
}

[[nodiscard]] Domains full_domains(const Lattice &lattice) {
    Domains domains;
    domains.reserve(lattice.candidates.size());
    for (const auto &candidates : lattice.candidates) {
        domains.emplace_back(candidates.size(), true);
    }
    return domains;
}

[[nodiscard]] std::size_t active_count(const std::vector<bool> &domain) {
    return static_cast<std::size_t>(std::ranges::count(domain, true));
}

void remove_candidate(
    Domains &domains, const std::size_t token, const std::size_t candidate,
    const std::string_view rule,
    std::map<std::string, std::uint64_t, std::less<>> &removed, bool &changed) {
    if (domains[token][candidate]) {
        domains[token][candidate] = false;
        ++removed[std::string{rule}];
        changed = true;
    }
}

[[nodiscard]] Domains
propagate(const Lattice &lattice, const bool fragment,
          std::map<std::string, std::uint64_t, std::less<>> &removed,
          std::uint64_t &checks, std::uint64_t &iterations) {
    auto domains = full_domains(lattice);
    bool changed{true};
    while (changed) {
        changed = false;
        ++iterations;

        if (!fragment) {
            std::vector<std::size_t> tokens_with_finite;
            for (std::size_t token = 0; token < domains.size(); ++token) {
                const bool has_finite = [&] {
                    for (std::size_t candidate = 0;
                         candidate < domains[token].size(); ++candidate) {
                        ++checks;
                        if (domains[token][candidate] &&
                            is_finite(lattice.candidates[token][candidate])) {
                            return true;
                        }
                    }
                    return false;
                }();
                if (has_finite) {
                    tokens_with_finite.push_back(token);
                }
            }
            if (tokens_with_finite.size() == 1U) {
                const auto token = tokens_with_finite.front();
                for (std::size_t candidate = 0;
                     candidate < domains[token].size(); ++candidate) {
                    ++checks;
                    if (!is_finite(lattice.candidates[token][candidate])) {
                        remove_candidate(domains, token, candidate, "H002",
                                         removed, changed);
                    }
                }
            }
        }

        for (std::size_t token = 0; token < domains.size(); ++token) {
            for (std::size_t candidate = 0; candidate < domains[token].size();
                 ++candidate) {
                if (!domains[token][candidate]) {
                    continue;
                }
                const auto &value = lattice.candidates[token][candidate];
                if (const auto *preposition =
                        std::get_if<words::PrepositionMorphology>(
                            &value.morphology);
                    preposition != nullptr) {
                    bool supported{};
                    for (std::size_t later = token + 1U;
                         later < domains.size() && !supported; ++later) {
                        for (std::size_t other = 0;
                             other < domains[later].size(); ++other) {
                            ++checks;
                            if (!domains[later][other]) {
                                continue;
                            }
                            const auto features = nominal_features(
                                lattice.candidates[later][other]);
                            supported =
                                features &&
                                has_support(compare(features->grammatical_case,
                                                    preposition->governs));
                            if (supported) {
                                break;
                            }
                        }
                    }
                    if (!supported) {
                        remove_candidate(domains, token, candidate, "H005",
                                         removed, changed);
                        continue;
                    }
                }

                if (value.enclitic_que) {
                    bool supported{};
                    const auto current = nominal_features(value);
                    for (std::size_t previous = 0;
                         previous < token && !supported; ++previous) {
                        for (std::size_t other = 0;
                             other < domains[previous].size(); ++other) {
                            ++checks;
                            if (!domains[previous][other]) {
                                continue;
                            }
                            const auto &earlier =
                                lattice.candidates[previous][other];
                            const auto previous_features =
                                nominal_features(earlier);
                            supported =
                                current && previous_features
                                    ? has_support(compare(
                                          current->grammatical_case,
                                          previous_features->grammatical_case))
                                    : category(earlier) == category(value);
                            if (supported) {
                                break;
                            }
                        }
                    }
                    if (!supported) {
                        remove_candidate(domains, token, candidate, "H007",
                                         removed, changed);
                    }
                }
            }
        }
    }
    return domains;
}

enum class GacConstraintKind : std::uint8_t {
    finite_clause,
    preposition_case,
    enclitic_coordination,
};

struct GacConstraint final {
    GacConstraintKind kind{GacConstraintKind::finite_clause};
    std::string_view id;
    std::size_t anchor{};
    std::vector<std::size_t> scope;
};

[[nodiscard]] bool candidate_supports_case(const Candidate &candidate,
                                           words::GrammaticalCase required);

using SupportWitness = std::vector<CandidateRef>;
using Residue = std::optional<SupportWitness>;
using ResidueTable = std::vector<std::vector<std::vector<Residue>>>;

void add_witness(SupportWitness &witness, const std::size_t token,
                 const std::size_t candidate) {
    const CandidateRef value{token, candidate};
    if (std::ranges::find(witness, value) == witness.end()) {
        witness.push_back(value);
    }
}

[[nodiscard]] bool residue_is_active(const SupportWitness &witness,
                                     const Domains &domains,
                                     std::uint64_t &candidate_checks) {
    for (const auto &value : witness) {
        ++candidate_checks;
        if (value.token >= domains.size() ||
            value.candidate >= domains[value.token].size() ||
            !domains[value.token][value.candidate]) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] const RelationCandidate *
find_active_relation(const RelationLattice &relations, const Domains &domains,
                     const RelationCandidateKind kind,
                     const std::optional<CandidateRef> governor,
                     const std::optional<CandidateRef> dependent,
                     const std::optional<CandidateRef> fixed_scope_value,
                     std::uint64_t &checks) {
    for (const auto &relation : relations.candidates) {
        if (relation.kind != kind ||
            (governor && relation.governor != *governor) ||
            (dependent && relation.dependent != *dependent) ||
            (fixed_scope_value &&
             ((relation.governor.token == fixed_scope_value->token &&
               relation.governor != *fixed_scope_value) ||
              (relation.dependent.token == fixed_scope_value->token &&
               relation.dependent != *fixed_scope_value)))) {
            continue;
        }
        ++checks;
        if (relation_allowed(relation) &&
            relation_endpoint_active(domains, relation.governor) &&
            relation_endpoint_active(domains, relation.dependent)) {
            return &relation;
        }
    }
    return nullptr;
}

[[nodiscard]] bool
gac_value_has_support(const Lattice &lattice, const RelationLattice &relations,
                      const Domains &domains, const GacConstraint &constraint,
                      const std::size_t token, const std::size_t candidate,
                      std::uint64_t &checks, SupportWitness &witness) {
    witness.clear();
    add_witness(witness, token, candidate);
    const auto &value = lattice.candidates[token][candidate];
    if (constraint.kind == GacConstraintKind::finite_clause) {
        ++checks;
        if (is_finite(value)) {
            return true;
        }
        for (const auto other : constraint.scope) {
            if (other == token) {
                continue;
            }
            for (std::size_t option = 0; option < domains[other].size();
                 ++option) {
                if (!domains[other][option]) {
                    continue;
                }
                ++checks;
                if (is_finite(lattice.candidates[other][option])) {
                    add_witness(witness, other, option);
                    return true;
                }
            }
        }
        return false;
    }

    if (constraint.kind == GacConstraintKind::preposition_case) {
        if (token == constraint.anchor) {
            const auto *preposition =
                std::get_if<words::PrepositionMorphology>(&value.morphology);
            ++checks;
            if (preposition == nullptr) {
                return true;
            }
            const auto *support = find_active_relation(
                relations, domains,
                RelationCandidateKind::preposition_complement,
                CandidateRef{token, candidate}, std::nullopt, std::nullopt,
                checks);
            if (support != nullptr) {
                add_witness(witness, support->dependent.token,
                            support->dependent.candidate);
            }
            return support != nullptr;
        }
        for (std::size_t anchor_candidate = 0;
             anchor_candidate < domains[constraint.anchor].size();
             ++anchor_candidate) {
            if (!domains[constraint.anchor][anchor_candidate]) {
                continue;
            }
            const auto *preposition = std::get_if<words::PrepositionMorphology>(
                &lattice.candidates[constraint.anchor][anchor_candidate]
                     .morphology);
            ++checks;
            if (preposition == nullptr) {
                add_witness(witness, constraint.anchor, anchor_candidate);
                return true;
            }
            const auto *support = find_active_relation(
                relations, domains,
                RelationCandidateKind::preposition_complement,
                CandidateRef{constraint.anchor, anchor_candidate}, std::nullopt,
                CandidateRef{token, candidate}, checks);
            if (support != nullptr) {
                add_witness(witness, constraint.anchor, anchor_candidate);
                add_witness(witness, support->dependent.token,
                            support->dependent.candidate);
                return true;
            }
        }
        return false;
    }

    if (token == constraint.anchor) {
        ++checks;
        if (!value.enclitic_que) {
            return true;
        }
        const auto *support = find_active_relation(
            relations, domains, RelationCandidateKind::coordination,
            std::nullopt, CandidateRef{token, candidate}, std::nullopt, checks);
        if (support != nullptr) {
            add_witness(witness, support->governor.token,
                        support->governor.candidate);
        }
        return support != nullptr;
    }
    for (std::size_t anchor_candidate = 0;
         anchor_candidate < domains[constraint.anchor].size();
         ++anchor_candidate) {
        if (!domains[constraint.anchor][anchor_candidate]) {
            continue;
        }
        const auto &enclitic =
            lattice.candidates[constraint.anchor][anchor_candidate];
        ++checks;
        if (!enclitic.enclitic_que) {
            add_witness(witness, constraint.anchor, anchor_candidate);
            return true;
        }
        const auto *support = find_active_relation(
            relations, domains, RelationCandidateKind::coordination,
            std::nullopt, CandidateRef{constraint.anchor, anchor_candidate},
            CandidateRef{token, candidate}, checks);
        if (support != nullptr) {
            add_witness(witness, constraint.anchor, anchor_candidate);
            add_witness(witness, support->governor.token,
                        support->governor.candidate);
            return true;
        }
    }
    return false;
}

[[nodiscard]] std::vector<GacConstraint>
build_gac_constraints(const Lattice &lattice, const bool fragment) {
    std::vector<GacConstraint> constraints;
    if (!fragment) {
        GacConstraint finite{GacConstraintKind::finite_clause, "H002", 0U, {}};
        finite.scope.resize(lattice.candidates.size());
        std::iota(finite.scope.begin(), finite.scope.end(), 0U);
        constraints.push_back(std::move(finite));
    }
    for (std::size_t token = 0; token < lattice.candidates.size(); ++token) {
        if (token + 1U < lattice.candidates.size() &&
            std::ranges::any_of(
                lattice.candidates[token], [](const Candidate &candidate) {
                    return std::holds_alternative<words::PrepositionMorphology>(
                        candidate.morphology);
                })) {
            GacConstraint constraint{
                GacConstraintKind::preposition_case, "H005", token, {}};
            for (std::size_t scope_token = token;
                 scope_token < lattice.candidates.size(); ++scope_token) {
                constraint.scope.push_back(scope_token);
            }
            constraints.push_back(std::move(constraint));
        }
        if (token > 0U && std::ranges::any_of(lattice.candidates[token],
                                              &Candidate::enclitic_que)) {
            GacConstraint constraint{
                GacConstraintKind::enclitic_coordination, "H007", token, {}};
            constraint.scope.resize(token + 1U);
            std::iota(constraint.scope.begin(), constraint.scope.end(), 0U);
            constraints.push_back(std::move(constraint));
        }
    }
    return constraints;
}

[[nodiscard]] Domains
gac_propagate(const Lattice &lattice, const RelationLattice &relations,
              const bool fragment,
              std::map<std::string, std::uint64_t, std::less<>> &removed,
              std::uint64_t &checks, std::uint64_t &queue_pops,
              std::uint64_t &revisions, const bool use_residues,
              std::uint64_t &residue_hits, std::uint64_t &residue_misses,
              std::uint64_t &residue_invalidations,
              std::uint64_t &residue_candidate_checks) {
    auto domains = full_domains(lattice);
    const auto constraints = build_gac_constraints(lattice, fragment);
    ResidueTable residues;
    if (use_residues) {
        residues.resize(constraints.size());
        for (auto &constraint_residues : residues) {
            constraint_residues.resize(domains.size());
            for (std::size_t token = 0; token < domains.size(); ++token) {
                constraint_residues[token].resize(domains[token].size());
            }
        }
    }
    std::vector<std::vector<std::size_t>> adjacency(domains.size());
    for (std::size_t index = 0; index < constraints.size(); ++index) {
        for (const auto token : constraints[index].scope) {
            adjacency[token].push_back(index);
        }
    }
    std::queue<std::size_t> agenda;
    std::vector<bool> queued(constraints.size(), true);
    for (std::size_t index = 0; index < constraints.size(); ++index) {
        agenda.push(index);
    }
    while (!agenda.empty()) {
        const auto index = agenda.front();
        agenda.pop();
        queued[index] = false;
        ++queue_pops;
        const auto &constraint = constraints[index];
        for (const auto token : constraint.scope) {
            ++revisions;
            bool token_changed{};
            for (std::size_t candidate = 0; candidate < domains[token].size();
                 ++candidate) {
                if (!domains[token][candidate]) {
                    continue;
                }
                auto *residue =
                    use_residues ? &residues[index][token][candidate] : nullptr;
                if (residue != nullptr && *residue) {
                    if (residue_is_active(**residue, domains,
                                          residue_candidate_checks)) {
                        ++residue_hits;
                        continue;
                    }
                    ++residue_invalidations;
                    residue->reset();
                }
                if (residue != nullptr) {
                    ++residue_misses;
                }
                SupportWitness witness;
                if (gac_value_has_support(lattice, relations, domains,
                                          constraint, token, candidate, checks,
                                          witness)) {
                    if (residue != nullptr) {
                        *residue = std::move(witness);
                    }
                    continue;
                }
                remove_candidate(domains, token, candidate, constraint.id,
                                 removed, token_changed);
            }
            if (!token_changed) {
                continue;
            }
            for (const auto affected : adjacency[token]) {
                if (!queued[affected]) {
                    agenda.push(affected);
                    queued[affected] = true;
                }
            }
        }
    }
    return domains;
}

void enumerate_recursive(const Lattice &lattice,
                         const RelationLattice &relations,
                         const Domains &domains, const bool fragment,
                         const std::size_t token, Assignment &current,
                         Enumeration &result) {
    if (token == domains.size()) {
        ++result.states;
        ++result.complete;
        const auto violation = first_violation(lattice, relations, current,
                                               fragment, result.checks);
        if (violation) {
            ++result.rejections[std::string{*violation}];
            ++result.backtracks;
        } else {
            result.valid.push_back(current);
        }
        return;
    }
    for (std::size_t candidate = 0; candidate < domains[token].size();
         ++candidate) {
        if (!domains[token][candidate]) {
            continue;
        }
        ++result.states;
        current[token] = candidate;
        enumerate_recursive(lattice, relations, domains, fragment, token + 1U,
                            current, result);
    }
}

[[nodiscard]] Enumeration enumerate(const Lattice &lattice,
                                    const RelationLattice &relations,
                                    const Domains &domains,
                                    const bool fragment) {
    Enumeration result;
    Assignment current(domains.size());
    enumerate_recursive(lattice, relations, domains, fragment, 0U, current,
                        result);
    return result;
}

using PartialAssignment = std::vector<std::optional<std::size_t>>;

[[nodiscard]] std::optional<std::string_view>
incremental_violation(const Lattice &lattice, const PartialAssignment &partial,
                      const std::size_t assigned_token, std::uint64_t &checks) {
    const auto candidate_index = partial[assigned_token];
    if (!candidate_index) {
        return std::nullopt;
    }
    const auto &candidate =
        lattice.candidates[assigned_token][*candidate_index];
    if (!candidate.enclitic_que) {
        return std::nullopt;
    }
    ++checks;
    const auto current = nominal_features(candidate);
    for (std::size_t previous = 0; previous < assigned_token; ++previous) {
        if (!partial[previous]) {
            continue;
        }
        const auto &earlier = lattice.candidates[previous][*partial[previous]];
        const auto earlier_features = nominal_features(earlier);
        if (current && earlier_features &&
            has_support(compare(current->grammatical_case,
                                earlier_features->grammatical_case))) {
            return std::nullopt;
        }
        if (!current && category(earlier) == category(candidate)) {
            return std::nullopt;
        }
    }
    return "H007";
}

[[nodiscard]] bool
candidate_supports_case(const Candidate &candidate,
                        const words::GrammaticalCase required) {
    const auto features = nominal_features(candidate);
    return features &&
           has_support(compare(features->grammatical_case, required));
}

[[nodiscard]] bool possible_case_support(const Lattice &lattice,
                                         const Domains &domains,
                                         const PartialAssignment &partial,
                                         const std::size_t begin,
                                         const words::GrammaticalCase required,
                                         std::uint64_t &checks) {
    for (std::size_t token = begin; token < partial.size(); ++token) {
        if (partial[token]) {
            ++checks;
            if (candidate_supports_case(
                    lattice.candidates[token][*partial[token]], required)) {
                return true;
            }
            continue;
        }
        for (std::size_t candidate = 0; candidate < domains[token].size();
             ++candidate) {
            if (!domains[token][candidate]) {
                continue;
            }
            ++checks;
            if (candidate_supports_case(lattice.candidates[token][candidate],
                                        required)) {
                return true;
            }
        }
    }
    return false;
}

[[nodiscard]] std::optional<std::string_view>
forward_violation(const Lattice &lattice, const Domains &domains,
                  const PartialAssignment &partial, const bool fragment,
                  std::uint64_t &checks) {
    if (!fragment) {
        bool possible_finite{};
        for (std::size_t token = 0; token < partial.size() && !possible_finite;
             ++token) {
            if (partial[token]) {
                ++checks;
                possible_finite =
                    is_finite(lattice.candidates[token][*partial[token]]);
                continue;
            }
            for (std::size_t candidate = 0; candidate < domains[token].size();
                 ++candidate) {
                if (!domains[token][candidate]) {
                    continue;
                }
                ++checks;
                if (is_finite(lattice.candidates[token][candidate])) {
                    possible_finite = true;
                    break;
                }
            }
        }
        if (!possible_finite) {
            return "H002";
        }
    }

    for (std::size_t token = 0; token < partial.size(); ++token) {
        if (!partial[token]) {
            continue;
        }
        const auto &value = lattice.candidates[token][*partial[token]];
        if (const auto *preposition =
                std::get_if<words::PrepositionMorphology>(&value.morphology);
            preposition != nullptr &&
            !possible_case_support(lattice, domains, partial, token + 1U,
                                   preposition->governs, checks)) {
            return "H005";
        }
        if (value.enclitic_que) {
            bool supported{};
            const auto current = nominal_features(value);
            for (std::size_t previous = 0; previous < token && !supported;
                 ++previous) {
                if (partial[previous]) {
                    ++checks;
                    const auto &earlier =
                        lattice.candidates[previous][*partial[previous]];
                    const auto earlier_features = nominal_features(earlier);
                    supported = current && earlier_features
                                    ? has_support(compare(
                                          current->grammatical_case,
                                          earlier_features->grammatical_case))
                                    : category(earlier) == category(value);
                    continue;
                }
                for (std::size_t candidate = 0;
                     candidate < domains[previous].size(); ++candidate) {
                    if (!domains[previous][candidate]) {
                        continue;
                    }
                    ++checks;
                    const auto &earlier =
                        lattice.candidates[previous][candidate];
                    const auto earlier_features = nominal_features(earlier);
                    supported = current && earlier_features
                                    ? has_support(compare(
                                          current->grammatical_case,
                                          earlier_features->grammatical_case))
                                    : category(earlier) == category(value);
                    if (supported) {
                        break;
                    }
                }
            }
            if (!supported) {
                return "H007";
            }
        }
    }
    return std::nullopt;
}

void enumerate_incremental_recursive(
    const Lattice &lattice, const RelationLattice &relations,
    const Domains &domains, const bool fragment, const std::size_t token,
    PartialAssignment &partial, Enumeration &result) {
    if (token == domains.size()) {
        ++result.states;
        ++result.complete;
        Assignment assignment(partial.size());
        for (std::size_t index = 0; index < partial.size(); ++index) {
            assignment[index] = *partial[index];
        }
        const auto violation = first_violation(lattice, relations, assignment,
                                               fragment, result.checks);
        if (violation) {
            ++result.rejections[std::string{*violation}];
            ++result.backtracks;
        } else {
            result.valid.push_back(std::move(assignment));
        }
        return;
    }
    for (std::size_t candidate = 0; candidate < domains[token].size();
         ++candidate) {
        if (!domains[token][candidate]) {
            continue;
        }
        ++result.states;
        partial[token] = candidate;
        if (const auto violation =
                incremental_violation(lattice, partial, token, result.checks)) {
            ++result.rejections[std::string{*violation}];
            ++result.backtracks;
        } else {
            enumerate_incremental_recursive(lattice, relations, domains,
                                            fragment, token + 1U, partial,
                                            result);
        }
        partial[token].reset();
    }
}

[[nodiscard]] Enumeration
enumerate_incremental(const Lattice &lattice, const RelationLattice &relations,
                      const Domains &domains, const bool fragment) {
    Enumeration result;
    PartialAssignment partial(domains.size());
    enumerate_incremental_recursive(lattice, relations, domains, fragment, 0U,
                                    partial, result);
    return result;
}

void enumerate_mrv_recursive(const Lattice &lattice,
                             const RelationLattice &relations,
                             const Domains &domains, const bool fragment,
                             PartialAssignment &partial,
                             const std::size_t assigned, Enumeration &result) {
    if (assigned == partial.size()) {
        ++result.states;
        ++result.complete;
        Assignment assignment(partial.size());
        for (std::size_t token = 0; token < partial.size(); ++token) {
            assignment[token] = *partial[token];
        }
        const auto violation = first_violation(lattice, relations, assignment,
                                               fragment, result.checks);
        if (violation) {
            ++result.rejections[std::string{*violation}];
            ++result.backtracks;
        } else {
            result.valid.push_back(std::move(assignment));
        }
        return;
    }

    std::optional<std::size_t> selected;
    std::size_t smallest = std::numeric_limits<std::size_t>::max();
    for (std::size_t token = 0; token < partial.size(); ++token) {
        if (partial[token]) {
            continue;
        }
        const auto count = active_count(domains[token]);
        if (count < smallest) {
            selected = token;
            smallest = count;
        }
    }
    if (!selected) {
        return;
    }
    for (std::size_t candidate = 0; candidate < domains[*selected].size();
         ++candidate) {
        if (!domains[*selected][candidate]) {
            continue;
        }
        ++result.states;
        partial[*selected] = candidate;
        if (const auto violation = forward_violation(lattice, domains, partial,
                                                     fragment, result.checks)) {
            ++result.rejections[std::string{*violation}];
            ++result.backtracks;
        } else {
            enumerate_mrv_recursive(lattice, relations, domains, fragment,
                                    partial, assigned + 1U, result);
        }
        partial[*selected].reset();
    }
}

[[nodiscard]] Enumeration
enumerate_mrv_forward_checking(const Lattice &lattice,
                               const RelationLattice &relations,
                               const Domains &domains, const bool fragment) {
    Enumeration result;
    PartialAssignment partial(domains.size());
    enumerate_mrv_recursive(lattice, relations, domains, fragment, partial, 0U,
                            result);
    return result;
}

void add_score_reason(std::vector<ScoreReason> *const reasons,
                      const std::string_view id, const double delta,
                      std::string detail) {
    if (reasons != nullptr) {
        reasons->push_back(
            ScoreReason{std::string{id}, delta, std::move(detail)});
    }
}

[[nodiscard]] double candidate_score(const Candidate &candidate,
                                     const std::size_t token,
                                     std::vector<ScoreReason> *const reasons) {
    double score{};
    const auto lexical_weight = [](const words::LexicalFrequency frequency) {
        constexpr std::array weights{
            std::pair{words::LexicalFrequency::very_frequent, 11.0},
            std::pair{words::LexicalFrequency::frequent, 10.0},
            std::pair{words::LexicalFrequency::common, 9.0},
            std::pair{words::LexicalFrequency::lesser, 8.0},
            std::pair{words::LexicalFrequency::uncommon, 7.0},
            std::pair{words::LexicalFrequency::very_rare, 6.0},
            std::pair{words::LexicalFrequency::inscription, 5.0},
            std::pair{words::LexicalFrequency::graffiti, 4.0},
            std::pair{words::LexicalFrequency::pliny, 5.0},
        };
        const auto found = std::ranges::find(
            weights, frequency,
            &std::pair<words::LexicalFrequency, double>::first);
        return found == weights.end() ? 0.0 : found->second;
    };
    if (candidate.lexical_frequency != words::LexicalFrequency::unknown) {
        const double delta = lexical_weight(candidate.lexical_frequency);
        score += delta;
        add_score_reason(reasons, "S001", delta,
                         "token " + std::to_string(token) + " lemma " +
                             candidate.lemma + " lexical-frequency");
    }
    const auto rule_weight = [](const words::RuleFrequency frequency) {
        constexpr std::array weights{
            std::pair{words::RuleFrequency::most_frequent, 5.5},
            std::pair{words::RuleFrequency::sometimes, 5.0},
            std::pair{words::RuleFrequency::uncommon, 4.5},
            std::pair{words::RuleFrequency::infrequent, 4.0},
            std::pair{words::RuleFrequency::rare, 3.5},
            std::pair{words::RuleFrequency::very_rare, 3.0},
            std::pair{words::RuleFrequency::inscription, 2.5},
            std::pair{words::RuleFrequency::reserved_m, 0.0},
            std::pair{words::RuleFrequency::reserved_n, 0.0},
        };
        const auto found =
            std::ranges::find(weights, frequency,
                              &std::pair<words::RuleFrequency, double>::first);
        return found == weights.end() ? 0.0 : found->second;
    };
    if (candidate.rule_frequency != words::RuleFrequency::unknown) {
        const double delta = rule_weight(candidate.rule_frequency);
        score += delta;
        add_score_reason(reasons, "S002", delta,
                         "token " + std::to_string(token) +
                             " inflection-frequency");
    } else if (!candidate.has_rule) {
        // UNIQUES has no fabricated inflection rule. Its absence is not an
        // unknown-frequency penalty; treat it like an ordinary common rule.
        score += 5.5;
        add_score_reason(reasons, "S002", 5.5,
                         "token " + std::to_string(token) +
                             " unique-form-without-rule");
    }
    if (is_finite(candidate)) {
        score += 2.0;
        add_score_reason(reasons, "S012", 2.0,
                         "token " + std::to_string(token) +
                             " finite-clause-anchor");
    }
    return score;
}

[[nodiscard]] double
assignment_score(const Lattice &lattice, const Assignment &assignment,
                 const RelationLattice *const relation_lattice = nullptr,
                 std::vector<ScoreReason> *const reasons = nullptr) {
    double score{};
    const auto choice = resolve(lattice, assignment);
    for (std::size_t token = 0; token < assignment.size(); ++token) {
        score += candidate_score(lattice.candidates[token][assignment[token]],
                                 token, reasons);
    }
    // Experimental preferences have their own IDs; they do not overload the
    // S004/S005/S008 definitions frozen in ROADMAP.md.
    for (const auto *candidate : choice) {
        if (candidate->verb_kind == words::VerbKind::to_be ||
            candidate->verb_kind == words::VerbKind::compound_of_to_be) {
            score += 2.0;
            add_score_reason(reasons, "S013", 2.0, "known copular verb kind");
        }
        if (is_modifier(*candidate)) {
            const bool has_head =
                std::ranges::any_of(choice, [&](const Candidate *other) {
                    return is_noun_like(*other) && agrees(*candidate, *other);
                });
            const double delta = has_head ? 4.0 : -0.5;
            score += delta;
            add_score_reason(reasons, "S011", delta,
                             has_head ? "modifier has agreeing nominal"
                                      : "substantive/unattached modifier");
        }
        if (const auto *verb =
                std::get_if<words::VerbMorphology>(&candidate->morphology);
            verb != nullptr && is_finite(*candidate) &&
            verb->person == words::Person::third) {
            const bool has_subject =
                std::ranges::any_of(choice, [&](const Candidate *other) {
                    const auto features = nominal_features(*other);
                    return is_noun_like(*other) && features &&
                           features->grammatical_case ==
                               words::GrammaticalCase::nominative &&
                           has_support(compare(features->number, verb->number));
                });
            if (has_subject) {
                score += 3.0;
                add_score_reason(reasons, "S012", 3.0,
                                 "third-person predicate has agreeing subject");
            }
        }
    }
    if (relation_lattice != nullptr) {
        std::set<std::pair<std::size_t, std::size_t>> rewarded_comparisons;
        for (const auto &relation : relation_lattice->candidates) {
            if (relation.compatibility != Compatibility::compatible ||
                !relation_selected(relation, assignment)) {
                continue;
            }
            if (relation.kind ==
                    RelationCandidateKind::comparison_standard &&
                rewarded_comparisons
                    .emplace(relation.governor.token,
                             relation.dependent.token)
                    .second) {
                score += 4.0;
                add_score_reason(
                    reasons, "S015", 4.0,
                    "licensed comparison standard: token " +
                        std::to_string(relation.governor.token) +
                        " -> token " +
                        std::to_string(relation.dependent.token));
                continue;
            }
            if (relation.kind != RelationCandidateKind::verb_argument) {
                continue;
            }
            const auto &predicate =
                lattice.candidates[relation.governor.token]
                                  [relation.governor.candidate];
            if (!words::governed_case(predicate.verb_kind)) {
                continue;
            }
            score += 4.0;
            add_score_reason(reasons, "S008", 4.0,
                             "known government: token " +
                                 std::to_string(relation.governor.token) +
                                 " -> token " +
                                 std::to_string(relation.dependent.token));
        }
    }
    return score;
}

[[nodiscard]] bool matches_preferred_lemmas(const Lattice &lattice,
                                            const Assignment &assignment,
                                            const Fixture &fixture) {
    if (fixture.preferred_lemmas.size() != assignment.size()) {
        return false;
    }
    for (std::size_t token = 0; token < assignment.size(); ++token) {
        if (lattice.candidates[token][assignment[token]].lemma !=
            fixture.preferred_lemmas[token]) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool
matches_morphology_alternative(const Candidate &candidate,
                               const MorphologyGoldAlternative &expected) {
    if (expected.lemma && candidate.lemma != *expected.lemma) {
        return false;
    }
    if (expected.part && surface_part_name(candidate.part) != *expected.part) {
        return false;
    }
    return std::visit(
        [&](const auto &value) {
            using T = std::remove_cvref_t<decltype(value)>;
            if (expected.grammatical_case || expected.gender) {
                if constexpr (std::is_same_v<T, words::NounMorphology> ||
                              std::is_same_v<T, words::PronounMorphology> ||
                              std::is_same_v<T, words::AdjectiveMorphology> ||
                              std::is_same_v<T, words::NumeralMorphology> ||
                              std::is_same_v<T, words::ParticipleMorphology> ||
                              std::is_same_v<T, words::SupineMorphology>) {
                    if (expected.grammatical_case &&
                        words::case_name(value.grammatical_case) !=
                            *expected.grammatical_case) {
                        return false;
                    }
                    if (expected.gender &&
                        words::gender_name(value.gender) != *expected.gender) {
                        return false;
                    }
                } else {
                    return false;
                }
            }
            if (expected.governs_case) {
                if constexpr (std::is_same_v<T, words::PrepositionMorphology>) {
                    if (words::case_name(value.governs) !=
                        *expected.governs_case) {
                        return false;
                    }
                } else {
                    return false;
                }
            }
            if (expected.number) {
                if constexpr (std::is_same_v<T, words::NounMorphology> ||
                              std::is_same_v<T, words::PronounMorphology> ||
                              std::is_same_v<T, words::AdjectiveMorphology> ||
                              std::is_same_v<T, words::NumeralMorphology> ||
                              std::is_same_v<T, words::ParticipleMorphology> ||
                              std::is_same_v<T, words::SupineMorphology> ||
                              std::is_same_v<T, words::VerbMorphology>) {
                    if (words::number_name(value.number) != *expected.number) {
                        return false;
                    }
                } else {
                    return false;
                }
            }
            if (expected.degree) {
                if constexpr (std::is_same_v<T,
                                             words::AdjectiveMorphology> ||
                              std::is_same_v<T, words::AdverbMorphology>) {
                    if (words::degree_name(value.degree) != *expected.degree) {
                        return false;
                    }
                } else {
                    return false;
                }
            }
            if (expected.tense || expected.voice || expected.mood ||
                expected.person) {
                if constexpr (std::is_same_v<T, words::VerbMorphology>) {
                    if (expected.tense &&
                        words::tense_name(value.tense) != *expected.tense) {
                        return false;
                    }
                    if (expected.voice &&
                        words::voice_name(value.voice) != *expected.voice) {
                        return false;
                    }
                    if (expected.mood &&
                        words::mood_name(value.mood) != *expected.mood) {
                        return false;
                    }
                    if (expected.person &&
                        std::to_underlying(value.person) != *expected.person) {
                        return false;
                    }
                } else {
                    return false;
                }
            }
            return true;
        },
        candidate.morphology);
}

[[nodiscard]] bool matches_morphology_gold(const Lattice &lattice,
                                           const Assignment &assignment,
                                           const Fixture &fixture) {
    if (!fixture.gold || fixture.gold->morphology.size() != assignment.size()) {
        return false;
    }
    for (const auto &token_gold : fixture.gold->morphology) {
        if (token_gold.token >= assignment.size() ||
            std::ranges::none_of(
                token_gold.alternatives,
                [&](const MorphologyGoldAlternative &alternative) {
                    return matches_morphology_alternative(
                        lattice.candidates[token_gold.token]
                                          [assignment[token_gold.token]],
                        alternative);
                })) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool morphology_gold_in_lattice(const Lattice &lattice,
                                              const Fixture &fixture) {
    if (!fixture.gold ||
        fixture.gold->morphology.size() != lattice.candidates.size()) {
        return false;
    }
    for (const auto &token_gold : fixture.gold->morphology) {
        if (token_gold.token >= lattice.candidates.size() ||
            std::ranges::none_of(
                lattice.candidates[token_gold.token],
                [&](const Candidate &candidate) {
                    return std::ranges::any_of(
                        token_gold.alternatives,
                        [&](const MorphologyGoldAlternative &alternative) {
                            return matches_morphology_alternative(candidate,
                                                                  alternative);
                        });
                })) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool same_relations(std::vector<Relation> left,
                                  std::vector<Relation> right) {
    const auto key = [](const Relation &relation) {
        return std::tuple{relation.dependent, relation.head, relation.label};
    };
    std::ranges::sort(left, {}, key);
    std::ranges::sort(right, {}, key);
    return left == right;
}

[[nodiscard]] bool matches_dependency_gold(const std::vector<Relation> &actual,
                                           const Fixture &fixture) {
    return fixture.gold &&
           std::ranges::any_of(fixture.gold->accepted_dependencies,
                               [&](const std::vector<Relation> &expected) {
                                   return same_relations(actual, expected);
                               });
}

[[nodiscard]] std::string assignment_id(const Assignment &assignment) {
    std::ostringstream output;
    for (std::size_t token = 0; token < assignment.size(); ++token) {
        if (token != 0U) {
            output << ',';
        }
        output << token << ':' << assignment[token];
    }
    return output.str();
}

[[nodiscard]] std::string
survivor_digest(const std::vector<std::string> &sorted_ids) {
    cpp_int hash{14695981039346656037ULL};
    const cpp_int mask = (cpp_int{1} << 64U) - 1U;
    for (const auto &id : sorted_ids) {
        for (const auto byte : id) {
            hash ^= static_cast<unsigned char>(byte);
            hash *= 1099511628211ULL;
            hash &= mask;
        }
        hash ^= static_cast<unsigned char>('\n');
        hash *= 1099511628211ULL;
        hash &= mask;
    }
    std::ostringstream output;
    output << std::hex << std::setfill('0') << std::setw(16)
           << hash.convert_to<std::uint64_t>();
    return output.str();
}

[[nodiscard]] const RelationCandidate *
find_selected_relation(const RelationLattice &relations,
                       const Assignment &assignment,
                       const RelationCandidateKind kind,
                       const std::optional<std::size_t> governor_token,
                       const std::optional<std::size_t> dependent_token,
                       const bool prefer_nearest_left = false) {
    const RelationCandidate *selected{};
    for (const auto &relation : relations.candidates) {
        if (relation.kind != kind || !relation_allowed(relation) ||
            !relation_selected(relation, assignment) ||
            (governor_token && relation.governor.token != *governor_token) ||
            (dependent_token && relation.dependent.token != *dependent_token)) {
            continue;
        }
        if (!prefer_nearest_left || selected == nullptr ||
            relation.governor.token > selected->governor.token) {
            selected = &relation;
        }
        if (!prefer_nearest_left) {
            break;
        }
    }
    return selected;
}

void record_selected_relation(
    std::vector<const RelationCandidate *> *const selected,
    const RelationCandidate *const relation) {
    if (selected != nullptr && relation != nullptr &&
        std::ranges::find(*selected, relation) == selected->end()) {
        selected->push_back(relation);
    }
}

[[nodiscard]] RelationCandidateChoice
relation_candidate_choice(const Lattice &lattice,
                          const RelationCandidate &relation) {
    RelationCandidateChoice result{
        std::string{relation_kind_name(relation.kind)},
        relation.governor.token,
        lattice.candidates[relation.governor.token][relation.governor.candidate]
            .source_index,
        relation.dependent.token,
        lattice.candidates[relation.dependent.token]
                          [relation.dependent.candidate]
            .source_index,
        {},
        std::string{relation.constraint_id},
        std::string{compatibility_name(relation.compatibility)},
    };
    for (const auto context : relation.contexts) {
        result.contexts.push_back(RelationCandidateContextChoice{
            context.token,
            lattice.candidates[context.token][context.candidate].source_index,
        });
    }
    return result;
}

[[nodiscard]] std::vector<Relation> dependency_relations(
    const Lattice &lattice, const RelationLattice &relation_lattice,
    const Assignment &assignment, const bool fragment,
    std::vector<const RelationCandidate *> *const selected = nullptr) {
    const auto choice = resolve(lattice, assignment);
    std::vector<Relation> relations;
    relations.reserve(choice.size());
    std::optional<std::size_t> root;
    for (std::size_t token = 0; token < choice.size(); ++token) {
        if (is_finite(*choice[token])) {
            root = token;
            break;
        }
    }
    if (!root && fragment && !choice.empty()) {
        for (std::size_t token = 0; token < choice.size(); ++token) {
            const auto features = nominal_features(*choice[token]);
            if (is_noun_like(*choice[token]) && features &&
                features->grammatical_case ==
                    words::GrammaticalCase::nominative) {
                root = token;
                break;
            }
        }
        for (std::size_t token = 0; token < choice.size(); ++token) {
            if (!root && is_noun_like(*choice[token])) {
                root = token;
                break;
            }
        }
        if (!root) {
            root = 0U;
        }
    }
    if (!root) {
        return {};
    }

    bool subject_assigned{};
    for (std::size_t token = 0; token < choice.size(); ++token) {
        if (token == *root) {
            relations.push_back(Relation{token, std::nullopt, "root"});
            continue;
        }
        const auto &candidate = *choice[token];
        if (is_finite(candidate)) {
            relations.push_back(Relation{token, root, "conj"});
            continue;
        }
        if (lattice.tokens[token].lookup == "quam" &&
            (candidate.part == words::PartOfSpeech::conjunction ||
             candidate.part == words::PartOfSpeech::adverb) &&
            token + 1U < choice.size()) {
            relations.push_back(Relation{token, token + 1U, "mark"});
            continue;
        }
        if (candidate.part == words::PartOfSpeech::conjunction) {
            const auto head = token + 1U < choice.size()
                                  ? std::optional<std::size_t>{token + 1U}
                                  : root;
            relations.push_back(Relation{token, head, "cc"});
            continue;
        }
        if (candidate.part == words::PartOfSpeech::adverb) {
            relations.push_back(Relation{token, root, "advmod"});
            continue;
        }
        if (candidate.part == words::PartOfSpeech::preposition) {
            const auto *attachment = find_selected_relation(
                relation_lattice, assignment,
                RelationCandidateKind::preposition_complement, token,
                std::nullopt);
            const auto object =
                attachment == nullptr
                    ? std::optional<std::size_t>{}
                    : std::optional<std::size_t>{attachment->dependent.token};
            record_selected_relation(selected, attachment);
            relations.push_back(
                Relation{token, object.value_or(*root), "case"});
            continue;
        }
        if (is_modifier(candidate)) {
            if (choice[*root]->verb_kind == words::VerbKind::to_be ||
                choice[*root]->verb_kind ==
                    words::VerbKind::compound_of_to_be) {
                relations.push_back(Relation{token, root, "predicative"});
                continue;
            }
            std::optional<std::size_t> head;
            std::size_t best_distance = std::numeric_limits<std::size_t>::max();
            for (std::size_t other = 0; other < choice.size(); ++other) {
                if (!is_noun_like(*choice[other]) ||
                    !agrees(candidate, *choice[other])) {
                    continue;
                }
                const auto distance =
                    token > other ? token - other : other - token;
                if (distance < best_distance) {
                    head = other;
                    best_distance = distance;
                }
            }
            relations.push_back(Relation{
                token, head.value_or(*root),
                head ? (candidate.part == words::PartOfSpeech::participle
                            ? "acl"
                            : "amod")
                     : "substantive"});
            continue;
        }
        if (is_noun_like(candidate)) {
            const auto features = nominal_features(candidate);
            const auto *comparison = find_selected_relation(
                relation_lattice, assignment,
                RelationCandidateKind::comparison_standard, std::nullopt,
                token);
            if (comparison != nullptr) {
                relations.push_back(Relation{token,
                                             comparison->governor.token,
                                             "obl:cmp"});
                record_selected_relation(selected, comparison);
                continue;
            }
            if (candidate.enclitic_que && features) {
                const auto *attachment =
                    find_selected_relation(relation_lattice, assignment,
                                           RelationCandidateKind::coordination,
                                           std::nullopt, token, true);
                if (attachment != nullptr) {
                    relations.push_back(
                        Relation{token, attachment->governor.token, "conj"});
                    record_selected_relation(selected, attachment);
                }
                if (relations.size() == token + 1U) {
                    continue;
                }
            }
            std::string label{"obl"};
            const auto *preposition_attachment = find_selected_relation(
                relation_lattice, assignment,
                RelationCandidateKind::preposition_complement, std::nullopt,
                token);
            const bool governed_by_preposition =
                preposition_attachment != nullptr;
            const auto *verb_attachment = find_selected_relation(
                relation_lattice, assignment,
                RelationCandidateKind::verb_argument, *root, token);
            const bool governed_by_verb =
                words::governed_case(choice[*root]->verb_kind).has_value() &&
                verb_attachment != nullptr &&
                verb_attachment->compatibility == Compatibility::compatible;
            if (governed_by_verb) {
                const auto required =
                    *words::governed_case(choice[*root]->verb_kind);
                label =
                    required == words::GrammaticalCase::dative ? "iobj" : "obl";
                record_selected_relation(selected, verb_attachment);
            }
            if (!governed_by_preposition && features &&
                features->grammatical_case ==
                    words::GrammaticalCase::nominative) {
                if (!subject_assigned) {
                    label = "nsubj";
                    subject_assigned = true;
                } else if (choice[*root]->verb_kind == words::VerbKind::to_be ||
                           choice[*root]->verb_kind ==
                               words::VerbKind::compound_of_to_be) {
                    label = "xcomp";
                } else {
                    label = "dislocated";
                }
            } else if (!governed_by_preposition && !governed_by_verb &&
                       features &&
                       features->grammatical_case ==
                           words::GrammaticalCase::accusative) {
                label = "obj";
            }
            relations.push_back(Relation{token, root, std::move(label)});
            continue;
        }
        relations.push_back(Relation{token, root, "dep"});
    }
    std::ranges::sort(relations, {}, &Relation::dependent);
    return relations;
}

struct AttachmentSlot final {
    bool required{};
    std::vector<std::size_t> relation_indices;
};

struct AttachmentEnumeration final {
    std::vector<std::vector<std::size_t>> analyses;
    std::uint64_t slots{};
    std::uint64_t partial_states{};
    std::uint64_t conflicts{};
    bool budget_exceeded{};
};

[[nodiscard]] std::vector<AttachmentSlot>
build_attachment_slots(const Lattice &lattice, const RelationLattice &relations,
                       const Assignment &assignment) {
    const auto choice = resolve(lattice, assignment);
    std::vector<AttachmentSlot> slots;
    for (std::size_t token = 0; token < choice.size(); ++token) {
        std::optional<RelationCandidateKind> required_kind;
        if (std::holds_alternative<words::PrepositionMorphology>(
                choice[token]->morphology)) {
            required_kind = RelationCandidateKind::preposition_complement;
        } else if (choice[token]->enclitic_que) {
            required_kind = RelationCandidateKind::coordination;
        }
        if (!required_kind) {
            continue;
        }
        AttachmentSlot slot{true, {}};
        for (std::size_t index = 0; index < relations.candidates.size();
             ++index) {
            const auto &relation = relations.candidates[index];
            const bool anchored =
                *required_kind == RelationCandidateKind::preposition_complement
                    ? relation.governor.token == token
                    : relation.dependent.token == token;
            if (relation.kind == *required_kind && anchored &&
                relation_allowed(relation) &&
                relation_selected(relation, assignment)) {
                slot.relation_indices.push_back(index);
            }
        }
        slots.push_back(std::move(slot));
    }

    for (std::size_t index = 0; index < relations.candidates.size(); ++index) {
        const auto &relation = relations.candidates[index];
        if (!relation_allowed(relation) ||
            !relation_selected(relation, assignment)) {
            continue;
        }
        if (relation.kind == RelationCandidateKind::comparison_standard) {
            slots.push_back(AttachmentSlot{false, {index}});
            continue;
        }
        if (relation.kind != RelationCandidateKind::verb_argument) {
            continue;
        }
        const auto &predicate = lattice.candidates[relation.governor.token]
                                                  [relation.governor.candidate];
        if (words::governed_case(predicate.verb_kind)) {
            slots.push_back(AttachmentSlot{false, {index}});
        }
    }
    return slots;
}

void enumerate_attachment_slots(const RelationLattice &relations,
                                const std::vector<AttachmentSlot> &slots,
                                const std::size_t slot_index,
                                std::vector<std::size_t> &current,
                                std::vector<bool> &occupied_dependents,
                                const std::uint64_t max_analyses,
                                AttachmentEnumeration &result) {
    if (result.budget_exceeded) {
        return;
    }
    if (slot_index == slots.size()) {
        if (static_cast<std::uint64_t>(result.analyses.size()) >=
            max_analyses) {
            result.budget_exceeded = true;
            return;
        }
        result.analyses.push_back(current);
        return;
    }
    const auto &slot = slots[slot_index];
    if (!slot.required) {
        ++result.partial_states;
        enumerate_attachment_slots(relations, slots, slot_index + 1U, current,
                                   occupied_dependents, max_analyses, result);
    }
    for (const auto relation_index : slot.relation_indices) {
        ++result.partial_states;
        const auto dependent =
            relations.candidates[relation_index].dependent.token;
        if (occupied_dependents[dependent]) {
            ++result.conflicts;
            continue;
        }
        occupied_dependents[dependent] = true;
        current.push_back(relation_index);
        enumerate_attachment_slots(relations, slots, slot_index + 1U, current,
                                   occupied_dependents, max_analyses, result);
        current.pop_back();
        occupied_dependents[dependent] = false;
    }
}

[[nodiscard]] AttachmentEnumeration
enumerate_attachments(const Lattice &lattice, const RelationLattice &relations,
                      const Assignment &assignment,
                      const std::uint64_t max_analyses) {
    AttachmentEnumeration result;
    const auto slots = build_attachment_slots(lattice, relations, assignment);
    result.slots = slots.size();
    std::vector<std::size_t> current;
    std::vector<bool> occupied_dependents(lattice.tokens.size());
    enumerate_attachment_slots(relations, slots, 0U, current,
                               occupied_dependents, max_analyses, result);
    return result;
}

[[nodiscard]] std::vector<std::size_t> projected_attachment_indices(
    const Lattice &lattice, const RelationLattice &relations,
    const Assignment &assignment, const bool fragment) {
    std::vector<const RelationCandidate *> selected;
    static_cast<void>(dependency_relations(lattice, relations, assignment,
                                           fragment, &selected));
    std::vector<std::size_t> indices;
    indices.reserve(selected.size());
    for (const auto *relation : selected) {
        indices.push_back(
            static_cast<std::size_t>(relation - relations.candidates.data()));
    }
    std::ranges::sort(indices);
    return indices;
}

[[nodiscard]] std::string
attachment_analysis_id(const Assignment &assignment,
                       const std::vector<std::size_t> &relation_indices) {
    std::string id = assignment_id(assignment) + "|r:";
    if (relation_indices.empty()) {
        id += '-';
        return id;
    }
    for (std::size_t index = 0; index < relation_indices.size(); ++index) {
        if (index != 0U) {
            id += ',';
        }
        id += std::to_string(relation_indices[index]);
    }
    return id;
}

struct DependencyArc final {
    Relation relation;
    double score{};
    std::string_view reason;
};

struct DependencyTreeAnalysis final {
    Assignment assignment;
    std::vector<DependencyArc> arcs;
    double arc_score{};
    bool projective{};
    std::string id;
};

struct DependencyTreeEnumeration final {
    std::vector<DependencyTreeAnalysis> analyses;
    std::uint64_t arc_candidates{};
    std::uint64_t partial_states{};
    std::uint64_t cycle_rejections{};
    std::uint64_t root_rejections{};
    bool budget_exceeded{};
};

[[nodiscard]] double distance_penalty(const std::size_t left,
                                      const std::size_t right) noexcept {
    const auto distance = left > right ? left - right : right - left;
    return 0.05 * static_cast<double>(distance);
}

void add_dependency_arc(std::vector<DependencyArc> &domain,
                        const std::size_t dependent,
                        const std::optional<std::size_t> head,
                        std::string label, const double score,
                        const std::string_view reason) {
    const auto duplicate =
        std::ranges::find_if(domain, [&](const DependencyArc &arc) {
            return arc.relation.dependent == dependent &&
                   arc.relation.head == head && arc.relation.label == label;
        });
    if (duplicate == domain.end()) {
        domain.push_back(DependencyArc{
            Relation{dependent, head, std::move(label)}, score, reason});
    } else if (score > duplicate->score) {
        duplicate->score = score;
        duplicate->reason = reason;
    }
}

[[nodiscard]] std::vector<std::vector<DependencyArc>>
build_dependency_arc_domains(const Lattice &lattice,
                             const RelationLattice &relations,
                             const Assignment &assignment,
                             const bool fragment) {
    const auto choice = resolve(lattice, assignment);
    std::vector<std::vector<DependencyArc>> domains(choice.size());
    std::vector<std::size_t> finite_tokens;
    for (std::size_t token = 0; token < choice.size(); ++token) {
        if (is_finite(*choice[token])) {
            finite_tokens.push_back(token);
        }
    }

    for (std::size_t dependent = 0; dependent < choice.size(); ++dependent) {
        const auto &candidate = *choice[dependent];
        auto &domain = domains[dependent];
        if (is_finite(candidate)) {
            add_dependency_arc(domain, dependent, std::nullopt, "root", 10.0,
                               "finite-root");
            for (const auto head : finite_tokens) {
                if (head != dependent) {
                    add_dependency_arc(domain, dependent, head, "conj",
                                       6.0 - distance_penalty(dependent, head),
                                       "finite-coordination");
                }
            }
            continue;
        }

        if (candidate.part == words::PartOfSpeech::preposition) {
            for (const auto &relation : relations.candidates) {
                if (relation.kind ==
                        RelationCandidateKind::preposition_complement &&
                    relation.governor.token == dependent &&
                    relation_allowed(relation) &&
                    relation_selected(relation, assignment)) {
                    add_dependency_arc(
                        domain, dependent, relation.dependent.token, "case",
                        9.0 - distance_penalty(dependent,
                                               relation.dependent.token),
                        "H005-preposition-complement");
                }
            }
            continue;
        }

        if (candidate.enclitic_que) {
            for (const auto &relation : relations.candidates) {
                if (relation.kind == RelationCandidateKind::coordination &&
                    relation.dependent.token == dependent &&
                    relation_allowed(relation) &&
                    relation_selected(relation, assignment)) {
                    add_dependency_arc(
                        domain, dependent, relation.governor.token, "conj",
                        9.0 - distance_penalty(dependent,
                                               relation.governor.token),
                        "H007-enclitic-coordination");
                }
            }
            continue;
        }

        if (lattice.tokens[dependent].lookup == "quam" &&
            (candidate.part == words::PartOfSpeech::conjunction ||
             candidate.part == words::PartOfSpeech::adverb) &&
            dependent + 1U < choice.size()) {
            add_dependency_arc(domain, dependent, dependent + 1U, "mark", 9.0,
                               "H011-comparison-marker");
        } else if (candidate.part == words::PartOfSpeech::conjunction ||
            candidate.part == words::PartOfSpeech::tackon) {
            if (dependent + 1U < choice.size()) {
                add_dependency_arc(domain, dependent, dependent + 1U, "cc", 6.0,
                                   "coordination-marker");
            }
        } else if (candidate.part == words::PartOfSpeech::adverb) {
            for (const auto head : finite_tokens) {
                add_dependency_arc(domain, dependent, head, "advmod",
                                   6.0 - distance_penalty(dependent, head),
                                   "adverb-to-predicate");
            }
        } else if (is_modifier(candidate)) {
            for (std::size_t head = 0; head < choice.size(); ++head) {
                if (head == dependent) {
                    continue;
                }
                if (is_noun_like(*choice[head]) &&
                    agrees(candidate, *choice[head])) {
                    add_dependency_arc(domain, dependent, head,
                                       candidate.part ==
                                               words::PartOfSpeech::participle
                                           ? "acl"
                                           : "amod",
                                       7.0 - distance_penalty(dependent, head),
                                       "nominal-agreement");
                }
                if (is_finite(*choice[head])) {
                    const bool copular =
                        choice[head]->verb_kind == words::VerbKind::to_be ||
                        choice[head]->verb_kind ==
                            words::VerbKind::compound_of_to_be;
                    add_dependency_arc(domain, dependent, head,
                                       copular ? "predicative" : "substantive",
                                       (copular ? 8.0 : 3.0) -
                                           distance_penalty(dependent, head),
                                       copular ? "copular-predicate"
                                               : "substantive-modifier");
                }
            }
        } else if (is_noun_like(candidate)) {
            const auto features = nominal_features(candidate);
            const auto *comparison = find_selected_relation(
                relations, assignment,
                RelationCandidateKind::comparison_standard, std::nullopt,
                dependent);
            if (comparison != nullptr) {
                add_dependency_arc(
                    domain, dependent, comparison->governor.token, "obl:cmp",
                    9.0 - distance_penalty(dependent,
                                           comparison->governor.token),
                    "H011-comparison-standard");
            }
            for (const auto head : finite_tokens) {
                const auto *government = find_selected_relation(
                    relations, assignment, RelationCandidateKind::verb_argument,
                    head, dependent);
                const bool governed =
                    government != nullptr &&
                    government->compatibility == Compatibility::compatible &&
                    words::governed_case(choice[head]->verb_kind).has_value();
                const bool prepositional = selected_relation_exists(
                    relations, assignment,
                    RelationCandidateKind::preposition_complement, std::nullopt,
                    dependent);
                std::string label{"obl"};
                double score{4.0};
                std::string_view reason{"nominal-oblique"};
                if (governed) {
                    const auto required =
                        *words::governed_case(choice[head]->verb_kind);
                    label = required == words::GrammaticalCase::dative ? "iobj"
                                                                       : "obl";
                    score = 9.0;
                    reason = "H006-known-government";
                } else if (prepositional) {
                    score = 8.0;
                    reason = "H005-prepositional-nominal";
                } else if (features && features->grammatical_case ==
                                           words::GrammaticalCase::nominative) {
                    label = "nsubj";
                    score = 8.0;
                    reason = "nominative-subject";
                } else if (features && features->grammatical_case ==
                                           words::GrammaticalCase::accusative) {
                    label = "obj";
                    score = 7.0;
                    reason = "accusative-object";
                }
                add_dependency_arc(domain, dependent, head, std::move(label),
                                   score - distance_penalty(dependent, head),
                                   reason);
                if (!governed && !prepositional && features &&
                    features->grammatical_case ==
                        words::GrammaticalCase::nominative) {
                    const bool copular =
                        choice[head]->verb_kind == words::VerbKind::to_be ||
                        choice[head]->verb_kind ==
                            words::VerbKind::compound_of_to_be;
                    add_dependency_arc(domain, dependent, head,
                                       copular ? "xcomp" : "dislocated",
                                       (copular ? 7.5 : 3.5) -
                                           distance_penalty(dependent, head),
                                       copular ? "copular-nominal-complement"
                                               : "secondary-nominative");
                }
            }
            for (std::size_t head = 0; head < choice.size(); ++head) {
                if (head != dependent && is_noun_like(*choice[head])) {
                    add_dependency_arc(domain, dependent, head, "nmod",
                                       2.0 - distance_penalty(dependent, head),
                                       "nominal-modifier");
                }
            }
        } else {
            for (const auto head : finite_tokens) {
                add_dependency_arc(domain, dependent, head,
                                   category(candidate) == Category::nonfinite
                                       ? "xcomp"
                                       : "dep",
                                   3.0 - distance_penalty(dependent, head),
                                   category(candidate) == Category::nonfinite
                                       ? "nonfinite-to-predicate"
                                       : "fallback-dependent");
            }
        }

        if (fragment) {
            const double root_score = is_noun_like(candidate) ? 8.0 : 1.0;
            add_dependency_arc(domain, dependent, std::nullopt, "root",
                               root_score, "fragment-root");
        }
        if (domain.empty()) {
            for (std::size_t head = 0; head < choice.size(); ++head) {
                if (head != dependent &&
                    choice[head]->part != words::PartOfSpeech::preposition) {
                    add_dependency_arc(domain, dependent, head, "dep",
                                       -distance_penalty(dependent, head),
                                       "fallback-dependent");
                }
            }
        }
    }
    return domains;
}

[[nodiscard]] bool
would_create_cycle(const std::size_t dependent, const std::size_t head,
                   const std::vector<std::optional<std::size_t>> &chosen,
                   const std::vector<std::vector<DependencyArc>> &domains) {
    auto current = head;
    while (current < chosen.size() && chosen[current]) {
        if (current == dependent) {
            return true;
        }
        const auto &arc = domains[current][*chosen[current]];
        if (!arc.relation.head) {
            return false;
        }
        current = *arc.relation.head;
    }
    return current == dependent;
}

[[nodiscard]] bool
tree_is_projective(const std::vector<DependencyArc> &arcs) noexcept {
    for (std::size_t left = 0; left < arcs.size(); ++left) {
        const auto left_dependent = arcs[left].relation.dependent + 1U;
        const auto left_head =
            arcs[left].relation.head ? *arcs[left].relation.head + 1U : 0U;
        const auto left_begin = std::min(left_dependent, left_head);
        const auto left_end = std::max(left_dependent, left_head);
        for (std::size_t right = left + 1U; right < arcs.size(); ++right) {
            const auto right_dependent = arcs[right].relation.dependent + 1U;
            const auto right_head = arcs[right].relation.head
                                        ? *arcs[right].relation.head + 1U
                                        : 0U;
            const auto right_begin = std::min(right_dependent, right_head);
            const auto right_end = std::max(right_dependent, right_head);
            if ((left_begin < right_begin && right_begin < left_end &&
                 left_end < right_end) ||
                (right_begin < left_begin && left_begin < right_end &&
                 right_end < left_end)) {
                return false;
            }
        }
    }
    return true;
}

[[nodiscard]] std::string
tree_analysis_id(const Assignment &assignment,
                 const std::vector<DependencyArc> &arcs) {
    std::string id = assignment_id(assignment) + "|t:";
    for (std::size_t dependent = 0; dependent < arcs.size(); ++dependent) {
        if (dependent != 0U) {
            id += ',';
        }
        id += std::to_string(dependent);
        id += '>';
        id += arcs[dependent].relation.head
                  ? std::to_string(*arcs[dependent].relation.head)
                  : "r";
        id += ':';
        id += arcs[dependent].relation.label;
    }
    return id;
}

void enumerate_dependency_tree_slots(
    const Assignment &assignment,
    const std::vector<std::vector<DependencyArc>> &domains,
    const std::vector<std::size_t> &order, const std::size_t slot,
    std::vector<std::optional<std::size_t>> &chosen,
    const std::size_t root_count, const std::uint64_t max_analyses,
    DependencyTreeEnumeration &result) {
    if (result.budget_exceeded) {
        return;
    }
    if (slot == order.size()) {
        if (root_count != 1U) {
            ++result.root_rejections;
            return;
        }
        if (static_cast<std::uint64_t>(result.analyses.size()) >=
            max_analyses) {
            result.budget_exceeded = true;
            return;
        }
        std::vector<DependencyArc> arcs(domains.size());
        double score{};
        for (std::size_t dependent = 0; dependent < domains.size();
             ++dependent) {
            arcs[dependent] = domains[dependent][*chosen[dependent]];
            score += arcs[dependent].score;
        }
        const auto projective = tree_is_projective(arcs);
        result.analyses.push_back(
            DependencyTreeAnalysis{assignment, arcs, score, projective,
                                   tree_analysis_id(assignment, arcs)});
        return;
    }

    const auto dependent = order[slot];
    for (std::size_t arc_index = 0; arc_index < domains[dependent].size();
         ++arc_index) {
        ++result.partial_states;
        const auto &arc = domains[dependent][arc_index];
        if (!arc.relation.head) {
            if (root_count != 0U) {
                ++result.root_rejections;
                continue;
            }
        } else if (would_create_cycle(dependent, *arc.relation.head, chosen,
                                      domains)) {
            ++result.cycle_rejections;
            continue;
        }
        chosen[dependent] = arc_index;
        enumerate_dependency_tree_slots(
            assignment, domains, order, slot + 1U, chosen,
            root_count + static_cast<std::size_t>(!arc.relation.head),
            max_analyses, result);
        chosen[dependent].reset();
    }
}

[[nodiscard]] DependencyTreeEnumeration
enumerate_dependency_trees(const Lattice &lattice,
                           const RelationLattice &relations,
                           const Assignment &assignment, const bool fragment,
                           const std::uint64_t max_analyses) {
    DependencyTreeEnumeration result;
    const auto domains =
        build_dependency_arc_domains(lattice, relations, assignment, fragment);
    for (const auto &domain : domains) {
        result.arc_candidates += domain.size();
    }
    if (domains.empty() || std::ranges::any_of(domains, [](const auto &domain) {
            return domain.empty();
        })) {
        return result;
    }
    std::vector<std::size_t> order(domains.size());
    std::iota(order.begin(), order.end(), 0U);
    std::ranges::stable_sort(order, [&](const auto left, const auto right) {
        return domains[left].size() < domains[right].size();
    });
    std::vector<std::optional<std::size_t>> chosen(domains.size());
    enumerate_dependency_tree_slots(assignment, domains, order, 0U, chosen, 0U,
                                    max_analyses, result);
    return result;
}

[[nodiscard]] std::string projected_tree_id(const Lattice &lattice,
                                            const RelationLattice &relations,
                                            const Assignment &assignment,
                                            const bool fragment) {
    auto projected =
        dependency_relations(lattice, relations, assignment, fragment);
    std::ranges::sort(projected, {}, &Relation::dependent);
    std::vector<DependencyArc> arcs;
    arcs.reserve(projected.size());
    for (auto &relation : projected) {
        arcs.push_back(DependencyArc{std::move(relation), 0.0, "projection"});
    }
    return tree_analysis_id(assignment, arcs);
}

struct DependencyDecoderOutput final {
    std::optional<DependencyTreeAnalysis> tree;
    std::uint64_t arc_candidates{};
    std::uint64_t states{};
    std::uint64_t cycles_contracted{};
};

struct EisnerCell final {
    double score{};
    std::size_t split{};
    bool reachable{};
};

[[nodiscard]] DependencyDecoderOutput
decode_eisner(const Lattice &lattice, const RelationLattice &relations,
              const Assignment &assignment, const bool fragment) {
    DependencyDecoderOutput output;
    const auto domains =
        build_dependency_arc_domains(lattice, relations, assignment, fragment);
    for (const auto &domain : domains) {
        output.arc_candidates += domain.size();
    }
    const auto token_count = domains.size();
    if (token_count == 0U ||
        std::ranges::any_of(
            domains, [](const auto &domain) { return domain.empty(); })) {
        return output;
    }

    std::optional<DependencyTreeAnalysis> best;
    for (std::size_t root_token = 0; root_token < token_count; ++root_token) {
        const auto root_arc = std::ranges::find_if(
            domains[root_token],
            [](const DependencyArc &arc) { return !arc.relation.head; });
        if (root_arc == domains[root_token].end()) {
            continue;
        }
        const auto node_count = token_count + 1U;
        std::vector<std::vector<const DependencyArc *>> best_arc(
            node_count,
            std::vector<const DependencyArc *>(node_count, nullptr));
        best_arc[0U][root_token + 1U] = &*root_arc;
        for (std::size_t dependent = 0; dependent < token_count; ++dependent) {
            if (dependent == root_token) {
                continue;
            }
            for (const auto &arc : domains[dependent]) {
                if (!arc.relation.head) {
                    continue;
                }
                const auto head = *arc.relation.head + 1U;
                const auto child = dependent + 1U;
                const auto *current = best_arc[head][child];
                if (current == nullptr || arc.score > current->score ||
                    (!(current->score > arc.score) &&
                     arc.relation.label < current->relation.label)) {
                    best_arc[head][child] = &arc;
                }
            }
        }

        using EisnerDirections = std::array<EisnerCell, 2U>;
        std::vector<std::vector<EisnerDirections>> complete(
            node_count, std::vector<EisnerDirections>(node_count));
        std::vector<std::vector<EisnerDirections>> incomplete(
            node_count, std::vector<EisnerDirections>(node_count));
        for (std::size_t index = 0; index < node_count; ++index) {
            complete[index][index][0U] = EisnerCell{0.0, index, true};
            complete[index][index][1U] = EisnerCell{0.0, index, true};
        }
        const auto update = [&](EisnerCell &cell, const double score,
                                const std::size_t split) {
            ++output.states;
            if (!cell.reachable || score > cell.score) {
                cell = EisnerCell{score, split, true};
            }
        };
        for (std::size_t width = 1U; width < node_count; ++width) {
            for (std::size_t left = 0; left + width < node_count; ++left) {
                const auto right = left + width;
                for (std::size_t split = left; split < right; ++split) {
                    const auto &left_complete = complete[left][split][1U];
                    const auto &right_complete =
                        complete[split + 1U][right][0U];
                    if (!left_complete.reachable || !right_complete.reachable) {
                        continue;
                    }
                    if (const auto *arc = best_arc[right][left];
                        arc != nullptr) {
                        update(incomplete[left][right][0U],
                               left_complete.score + right_complete.score +
                                   arc->score,
                               split);
                    }
                    if (const auto *arc = best_arc[left][right];
                        arc != nullptr) {
                        update(incomplete[left][right][1U],
                               left_complete.score + right_complete.score +
                                   arc->score,
                               split);
                    }
                }
                for (std::size_t split = left; split < right; ++split) {
                    const auto &left_complete = complete[left][split][0U];
                    const auto &right_incomplete = incomplete[split][right][0U];
                    if (left_complete.reachable && right_incomplete.reachable) {
                        update(complete[left][right][0U],
                               left_complete.score + right_incomplete.score,
                               split);
                    }
                }
                for (std::size_t split = left + 1U; split <= right; ++split) {
                    const auto &left_incomplete = incomplete[left][split][1U];
                    const auto &right_complete = complete[split][right][1U];
                    if (left_incomplete.reachable && right_complete.reachable) {
                        update(complete[left][right][1U],
                               left_incomplete.score + right_complete.score,
                               split);
                    }
                }
            }
        }
        if (!complete[0U][token_count][1U].reachable) {
            continue;
        }

        std::vector<const DependencyArc *> selected;
        std::function<void(std::size_t, std::size_t, bool, std::size_t)>
            reconstruct;
        reconstruct = [&](const std::size_t left, const std::size_t right,
                          const bool is_complete, const std::size_t direction) {
            if (left == right) {
                return;
            }
            const auto &cell = is_complete ? complete[left][right][direction]
                                           : incomplete[left][right][direction];
            const auto split = cell.split;
            if (!is_complete) {
                reconstruct(left, split, true, 1U);
                reconstruct(split + 1U, right, true, 0U);
                selected.push_back(direction == 0U ? best_arc[right][left]
                                                   : best_arc[left][right]);
            } else if (direction == 0U) {
                reconstruct(left, split, true, 0U);
                reconstruct(split, right, false, 0U);
            } else {
                reconstruct(left, split, false, 1U);
                reconstruct(split, right, true, 1U);
            }
        };
        reconstruct(0U, token_count, true, 1U);
        if (selected.size() != token_count ||
            std::ranges::any_of(
                selected, [](const auto *arc) { return arc == nullptr; })) {
            continue;
        }
        std::vector<DependencyArc> arcs(token_count);
        for (const auto *arc : selected) {
            arcs[arc->relation.dependent] = *arc;
        }
        DependencyTreeAnalysis candidate{
            assignment, arcs, complete[0U][token_count][1U].score, true,
            tree_analysis_id(assignment, arcs)};
        if (!best || candidate.arc_score > best->arc_score ||
            (!(best->arc_score > candidate.arc_score) &&
             candidate.id < best->id)) {
            best = std::move(candidate);
        }
    }
    output.tree = std::move(best);
    return output;
}

struct EdmondsEdge final {
    std::size_t from{};
    std::size_t to{};
    double score{};
    std::size_t original{};
};

struct EdmondsSelection final {
    bool success{};
    std::vector<std::size_t> originals;
};

[[nodiscard]] EdmondsSelection
chu_liu_edmonds(const std::size_t node_count, const std::size_t root,
                const std::vector<EdmondsEdge> &edges, std::uint64_t &states,
                std::uint64_t &cycles_contracted) {
    const auto absent = std::numeric_limits<std::size_t>::max();
    std::vector<std::optional<EdmondsEdge>> incoming(node_count);
    for (const auto &edge : edges) {
        ++states;
        if (edge.to == root || edge.from == edge.to) {
            continue;
        }
        auto &current = incoming[edge.to];
        if (!current || edge.score > current->score ||
            (!(current->score > edge.score) &&
             edge.original < current->original)) {
            current = edge;
        }
    }
    for (std::size_t node = 0; node < node_count; ++node) {
        if (node != root && !incoming[node]) {
            return {};
        }
    }

    std::vector<std::size_t> component(node_count, absent);
    std::vector<std::size_t> visited(node_count, absent);
    std::vector<std::vector<std::size_t>> cycles;
    for (std::size_t start = 0; start < node_count; ++start) {
        auto node = start;
        while (node != root && component[node] == absent &&
               visited[node] != start) {
            visited[node] = start;
            node = incoming[node]->from;
        }
        if (node == root || component[node] != absent ||
            visited[node] != start) {
            continue;
        }
        std::vector<std::size_t> cycle{node};
        for (auto member = incoming[node]->from; member != node;
             member = incoming[member]->from) {
            cycle.push_back(member);
        }
        const auto component_id = cycles.size();
        for (const auto member : cycle) {
            component[member] = component_id;
        }
        cycles.push_back(std::move(cycle));
    }
    if (cycles.empty()) {
        EdmondsSelection result{true, {}};
        result.originals.reserve(node_count - 1U);
        for (std::size_t node = 0; node < node_count; ++node) {
            if (node != root) {
                result.originals.push_back(incoming[node]->original);
            }
        }
        return result;
    }
    cycles_contracted += cycles.size();

    auto component_count = cycles.size();
    for (std::size_t node = 0; node < node_count; ++node) {
        if (component[node] == absent) {
            component[node] = component_count++;
        }
    }
    std::vector<bool> cycle_component(component_count);
    for (std::size_t index = 0; index < cycles.size(); ++index) {
        cycle_component[index] = true;
    }
    std::vector<EdmondsEdge> contracted;
    contracted.reserve(edges.size());
    for (const auto &edge : edges) {
        const auto from = component[edge.from];
        const auto to = component[edge.to];
        if (from == to) {
            continue;
        }
        auto score = edge.score;
        if (cycle_component[to]) {
            score -= incoming[edge.to]->score;
        }
        contracted.push_back(EdmondsEdge{from, to, score, edge.original});
    }
    auto selected = chu_liu_edmonds(component_count, component[root],
                                    contracted, states, cycles_contracted);
    if (!selected.success) {
        return {};
    }

    for (std::size_t cycle_id = 0; cycle_id < cycles.size(); ++cycle_id) {
        std::optional<std::size_t> entry_target;
        for (const auto original : selected.originals) {
            const auto found =
                std::ranges::find_if(edges, [&](const EdmondsEdge &edge) {
                    return edge.original == original &&
                           component[edge.from] != cycle_id &&
                           component[edge.to] == cycle_id;
                });
            if (found != edges.end()) {
                entry_target = found->to;
                break;
            }
        }
        if (!entry_target) {
            return {};
        }
        for (const auto member : cycles[cycle_id]) {
            if (member != *entry_target) {
                selected.originals.push_back(incoming[member]->original);
            }
        }
    }
    return selected;
}

[[nodiscard]] DependencyDecoderOutput
decode_mst(const Lattice &lattice, const RelationLattice &relations,
           const Assignment &assignment, const bool fragment) {
    DependencyDecoderOutput output;
    const auto domains =
        build_dependency_arc_domains(lattice, relations, assignment, fragment);
    for (const auto &domain : domains) {
        output.arc_candidates += domain.size();
    }
    const auto token_count = domains.size();
    if (token_count == 0U ||
        std::ranges::any_of(
            domains, [](const auto &domain) { return domain.empty(); })) {
        return output;
    }

    std::vector<const DependencyArc *> originals;
    std::vector<EdmondsEdge> all_edges;
    for (std::size_t dependent = 0; dependent < token_count; ++dependent) {
        std::map<std::size_t, const DependencyArc *> best_by_head;
        for (const auto &arc : domains[dependent]) {
            if (!arc.relation.head) {
                continue;
            }
            const auto head = *arc.relation.head;
            const auto found = best_by_head.find(head);
            if (found == best_by_head.end() ||
                arc.score > found->second->score ||
                (!(found->second->score > arc.score) &&
                 arc.relation.label < found->second->relation.label)) {
                best_by_head[head] = &arc;
            }
        }
        for (const auto &[head, arc] : best_by_head) {
            const auto original = originals.size();
            originals.push_back(arc);
            all_edges.push_back(
                EdmondsEdge{head, dependent, arc->score, original});
        }
    }

    std::optional<DependencyTreeAnalysis> best;
    for (std::size_t root_token = 0; root_token < token_count; ++root_token) {
        const auto root_arc = std::ranges::find_if(
            domains[root_token],
            [](const DependencyArc &arc) { return !arc.relation.head; });
        if (root_arc == domains[root_token].end()) {
            continue;
        }
        auto selected =
            chu_liu_edmonds(token_count, root_token, all_edges, output.states,
                            output.cycles_contracted);
        if (!selected.success ||
            selected.originals.size() != token_count - 1U) {
            continue;
        }
        std::vector<DependencyArc> arcs(token_count);
        arcs[root_token] = *root_arc;
        double score = root_arc->score;
        for (const auto original : selected.originals) {
            const auto *arc = originals[original];
            arcs[arc->relation.dependent] = *arc;
            score += arc->score;
        }
        const auto projective = tree_is_projective(arcs);
        DependencyTreeAnalysis candidate{assignment, arcs, score, projective,
                                         tree_analysis_id(assignment, arcs)};
        if (!best || candidate.arc_score > best->arc_score ||
            (!(best->arc_score > candidate.arc_score) &&
             candidate.id < best->id)) {
            best = std::move(candidate);
        }
    }
    output.tree = std::move(best);
    return output;
}

struct EarleyRule final {
    int lhs{};
    std::vector<int> rhs;
};

struct EarleyItem final {
    std::size_t rule{};
    std::size_t dot{};
    std::size_t origin{};
    auto operator<=>(const EarleyItem &) const = default;
};

[[nodiscard]] const std::vector<EarleyRule> &grammar(const GrammarMode mode) {
    static const std::vector<EarleyRule> complete_clause = [] {
        std::vector<EarleyRule> rules{
            {augmented_symbol, {sentence_symbol}},
            {sentence_symbol,
             {sequence_symbol, static_cast<int>(Category::finite),
              sequence_symbol}},
            {sequence_symbol, {sequence_symbol, unit_symbol}},
            {sequence_symbol, {}},
        };
        for (std::size_t terminal = 0; terminal < category_count; ++terminal) {
            rules.push_back(
                EarleyRule{unit_symbol, {static_cast<int>(terminal)}});
        }
        return rules;
    }();
    static const std::vector<EarleyRule> fragment = [] {
        std::vector<EarleyRule> rules{
            {augmented_symbol, {sentence_symbol}},
            {sentence_symbol, {sequence_symbol}},
            {sequence_symbol, {sequence_symbol, unit_symbol}},
            {sequence_symbol, {}},
        };
        for (std::size_t terminal = 0; terminal < category_count; ++terminal) {
            rules.push_back(
                EarleyRule{unit_symbol, {static_cast<int>(terminal)}});
        }
        return rules;
    }();
    return mode == GrammarMode::fragment ? fragment : complete_clause;
}

[[nodiscard]] bool is_nonterminal(const int symbol) noexcept {
    return symbol >= first_nonterminal;
}

[[nodiscard]] bool earley_accepts(const std::vector<Category> &input,
                                  const GrammarMode mode,
                                  std::uint64_t &created, std::uint64_t &reused,
                                  std::uint64_t &packed) {
    const auto &rules = grammar(mode);
    std::vector<std::set<EarleyItem>> chart(input.size() + 1U);
    chart[0].insert(EarleyItem{0U, 0U, 0U});
    ++created;
    for (std::size_t position = 0; position <= input.size(); ++position) {
        bool changed{true};
        while (changed) {
            changed = false;
            const std::vector<EarleyItem> snapshot(chart[position].begin(),
                                                   chart[position].end());
            for (const auto &item : snapshot) {
                const auto &rule = rules[item.rule];
                if (item.dot < rule.rhs.size() &&
                    is_nonterminal(rule.rhs[item.dot])) {
                    const auto wanted = rule.rhs[item.dot];
                    for (std::size_t candidate = 0; candidate < rules.size();
                         ++candidate) {
                        if (rules[candidate].lhs != wanted) {
                            continue;
                        }
                        const auto [unused, inserted] = chart[position].insert(
                            EarleyItem{candidate, 0U, position});
                        static_cast<void>(unused);
                        if (inserted) {
                            ++created;
                            changed = true;
                        } else {
                            ++reused;
                        }
                    }
                } else if (item.dot == rule.rhs.size()) {
                    const std::vector<EarleyItem> origins(
                        chart[item.origin].begin(), chart[item.origin].end());
                    for (const auto &parent : origins) {
                        const auto &parent_rule = rules[parent.rule];
                        if (parent.dot >= parent_rule.rhs.size() ||
                            parent_rule.rhs[parent.dot] != rule.lhs) {
                            continue;
                        }
                        const auto [unused, inserted] =
                            chart[position].insert(EarleyItem{
                                parent.rule, parent.dot + 1U, parent.origin});
                        static_cast<void>(unused);
                        if (inserted) {
                            ++created;
                            changed = true;
                        } else {
                            ++reused;
                        }
                    }
                }
            }
        }
        packed += chart[position].size();
        if (position == input.size()) {
            continue;
        }
        for (const auto &item : chart[position]) {
            const auto &rule = rules[item.rule];
            if (item.dot < rule.rhs.size() &&
                rule.rhs[item.dot] == static_cast<int>(input[position])) {
                const auto [unused, inserted] = chart[position + 1U].insert(
                    EarleyItem{item.rule, item.dot + 1U, item.origin});
                static_cast<void>(unused);
                if (inserted) {
                    ++created;
                } else {
                    ++reused;
                }
            }
        }
    }
    return chart.back().contains(EarleyItem{0U, 1U, 0U});
}

struct LrItem final {
    std::size_t production{};
    std::size_t dot{};
    auto operator<=>(const LrItem &) const = default;
};

enum class ActionKind : std::uint8_t { shift, reduce, accept };

struct Action final {
    ActionKind kind{ActionKind::shift};
    std::size_t value{};
    bool operator==(const Action &) const = default;
};

struct SlrTable final {
    std::map<std::pair<std::size_t, int>, std::vector<Action>> actions;
    std::map<std::pair<std::size_t, int>, std::size_t> gotos;
};

[[nodiscard]] std::set<LrItem> closure(std::set<LrItem> items,
                                       const GrammarMode mode) {
    const auto &rules = grammar(mode);
    bool changed{true};
    while (changed) {
        changed = false;
        const std::vector<LrItem> snapshot(items.begin(), items.end());
        for (const auto &item : snapshot) {
            const auto &rule = rules[item.production];
            if (item.dot >= rule.rhs.size() ||
                !is_nonterminal(rule.rhs[item.dot])) {
                continue;
            }
            const auto symbol = rule.rhs[item.dot];
            for (std::size_t production = 0; production < rules.size();
                 ++production) {
                if (rules[production].lhs == symbol) {
                    changed =
                        items.insert(LrItem{production, 0U}).second || changed;
                }
            }
        }
    }
    return items;
}

[[nodiscard]] std::set<LrItem> transition(const std::set<LrItem> &state,
                                          const int symbol,
                                          const GrammarMode mode) {
    const auto &rules = grammar(mode);
    std::set<LrItem> moved;
    for (const auto &item : state) {
        const auto &rule = rules[item.production];
        if (item.dot < rule.rhs.size() && rule.rhs[item.dot] == symbol) {
            moved.insert(LrItem{item.production, item.dot + 1U});
        }
    }
    return closure(std::move(moved), mode);
}

[[nodiscard]] SlrTable build_slr_table(const GrammarMode mode) {
    const auto &rules = grammar(mode);
    std::vector<std::set<LrItem>> states{
        closure(std::set<LrItem>{{0U, 0U}}, mode)};
    std::map<std::pair<std::size_t, int>, std::size_t> transitions;
    for (std::size_t state = 0; state < states.size(); ++state) {
        std::set<int> symbols;
        for (const auto &item : states[state]) {
            const auto &rule = rules[item.production];
            if (item.dot < rule.rhs.size()) {
                symbols.insert(rule.rhs[item.dot]);
            }
        }
        for (const auto symbol : symbols) {
            auto target = transition(states[state], symbol, mode);
            if (target.empty()) {
                continue;
            }
            const auto found = std::ranges::find(states, target);
            std::size_t target_index{};
            if (found == states.end()) {
                target_index = states.size();
                states.push_back(std::move(target));
            } else {
                target_index = static_cast<std::size_t>(found - states.begin());
            }
            transitions[{state, symbol}] = target_index;
        }
    }

    constexpr std::size_t nonterminal_count{4U};
    std::array<bool, nonterminal_count> nullable{};
    std::array<std::bitset<category_count>, nonterminal_count> first{};
    const auto nonterminal_index = [](const int symbol) {
        return static_cast<std::size_t>(symbol - first_nonterminal);
    };
    bool changed{true};
    while (changed) {
        changed = false;
        for (const auto &rule : rules) {
            const auto lhs = nonterminal_index(rule.lhs);
            bool all_nullable{true};
            for (const auto symbol : rule.rhs) {
                if (!is_nonterminal(symbol)) {
                    const auto before = first[lhs];
                    first[lhs].set(static_cast<std::size_t>(symbol));
                    changed = changed || before != first[lhs];
                    all_nullable = false;
                    break;
                }
                const auto rhs = nonterminal_index(symbol);
                const auto before = first[lhs];
                first[lhs] |= first[rhs];
                changed = changed || before != first[lhs];
                if (!nullable[rhs]) {
                    all_nullable = false;
                    break;
                }
            }
            if (all_nullable && !nullable[lhs]) {
                nullable[lhs] = true;
                changed = true;
            }
        }
    }

    std::array<std::bitset<category_count + 1U>, nonterminal_count> follow{};
    follow[nonterminal_index(augmented_symbol)].set(
        static_cast<std::size_t>(eof_symbol));
    changed = true;
    while (changed) {
        changed = false;
        for (const auto &rule : rules) {
            for (std::size_t position = 0; position < rule.rhs.size();
                 ++position) {
                const auto symbol = rule.rhs[position];
                if (!is_nonterminal(symbol)) {
                    continue;
                }
                const auto target = nonterminal_index(symbol);
                bool suffix_nullable{true};
                for (std::size_t later = position + 1U; later < rule.rhs.size();
                     ++later) {
                    const auto next = rule.rhs[later];
                    const auto before = follow[target];
                    if (is_nonterminal(next)) {
                        const auto next_index = nonterminal_index(next);
                        for (std::size_t terminal = 0;
                             terminal < category_count; ++terminal) {
                            if (first[next_index].test(terminal)) {
                                follow[target].set(terminal);
                            }
                        }
                        changed = changed || before != follow[target];
                        if (!nullable[next_index]) {
                            suffix_nullable = false;
                            break;
                        }
                    } else {
                        follow[target].set(static_cast<std::size_t>(next));
                        changed = changed || before != follow[target];
                        suffix_nullable = false;
                        break;
                    }
                }
                if (suffix_nullable) {
                    const auto before = follow[target];
                    follow[target] |= follow[nonterminal_index(rule.lhs)];
                    changed = changed || before != follow[target];
                }
            }
        }
    }

    SlrTable table;
    const auto add_action = [&](const std::size_t state, const int terminal,
                                const Action action) {
        auto &actions = table.actions[{state, terminal}];
        if (std::ranges::find(actions, action) == actions.end()) {
            actions.push_back(action);
        }
    };
    for (std::size_t state = 0; state < states.size(); ++state) {
        for (const auto &[key, target] : transitions) {
            if (key.first != state) {
                continue;
            }
            if (is_nonterminal(key.second)) {
                table.gotos[key] = target;
            } else {
                add_action(state, key.second,
                           Action{ActionKind::shift, target});
            }
        }
        for (const auto &item : states[state]) {
            const auto &rule = rules[item.production];
            if (item.dot != rule.rhs.size()) {
                continue;
            }
            if (item.production == 0U) {
                add_action(state, eof_symbol, Action{ActionKind::accept, 0U});
                continue;
            }
            const auto &lookaheads = follow[nonterminal_index(rule.lhs)];
            for (std::size_t terminal = 0; terminal < category_count + 1U;
                 ++terminal) {
                if (lookaheads.test(terminal)) {
                    add_action(state, static_cast<int>(terminal),
                               Action{ActionKind::reduce, item.production});
                }
            }
        }
    }
    return table;
}

[[nodiscard]] bool glr_accepts(const std::vector<Category> &input,
                               const GrammarMode mode, std::uint64_t &created,
                               std::uint64_t &reused, std::uint64_t &packed) {
    static const SlrTable complete_table =
        build_slr_table(GrammarMode::complete_clause);
    static const SlrTable fragment_table =
        build_slr_table(GrammarMode::fragment);
    const auto &table =
        mode == GrammarMode::fragment ? fragment_table : complete_table;
    const auto &rules = grammar(mode);
    std::set<std::vector<std::size_t>> active{{0U}};
    ++created;
    for (std::size_t position = 0; position <= input.size(); ++position) {
        const auto lookahead = position == input.size()
                                   ? eof_symbol
                                   : static_cast<int>(input[position]);
        std::queue<std::vector<std::size_t>> agenda;
        for (const auto &stack : active) {
            agenda.push(stack);
        }
        auto closure_stacks = active;
        bool accepted{};
        while (!agenda.empty()) {
            auto stack = std::move(agenda.front());
            agenda.pop();
            const auto action_it =
                table.actions.find({stack.back(), lookahead});
            if (action_it == table.actions.end()) {
                continue;
            }
            for (const auto &action : action_it->second) {
                if (action.kind == ActionKind::accept) {
                    accepted = true;
                    continue;
                }
                if (action.kind != ActionKind::reduce) {
                    continue;
                }
                const auto &production = rules[action.value];
                if (stack.size() <= production.rhs.size()) {
                    continue;
                }
                auto reduced = stack;
                reduced.resize(reduced.size() - production.rhs.size());
                const auto target =
                    table.gotos.find({reduced.back(), production.lhs});
                if (target == table.gotos.end()) {
                    continue;
                }
                reduced.push_back(target->second);
                const auto [unused, inserted] = closure_stacks.insert(reduced);
                static_cast<void>(unused);
                if (inserted) {
                    ++created;
                    agenda.push(std::move(reduced));
                } else {
                    ++reused;
                }
            }
        }
        packed += closure_stacks.size();
        if (position == input.size()) {
            return accepted;
        }
        std::set<std::vector<std::size_t>> shifted;
        for (const auto &stack : closure_stacks) {
            const auto action_it =
                table.actions.find({stack.back(), lookahead});
            if (action_it == table.actions.end()) {
                continue;
            }
            for (const auto &action : action_it->second) {
                if (action.kind != ActionKind::shift) {
                    continue;
                }
                auto next = stack;
                next.push_back(action.value);
                const auto [unused, inserted] = shifted.insert(std::move(next));
                static_cast<void>(unused);
                if (inserted) {
                    ++created;
                } else {
                    ++reused;
                }
            }
        }
        active = std::move(shifted);
        if (active.empty()) {
            return false;
        }
    }
    return false;
}

[[nodiscard]] std::vector<Category>
categories(const std::vector<const Candidate *> &choice) {
    std::vector<Category> result;
    result.reserve(choice.size());
    for (const auto *candidate : choice) {
        result.push_back(category(*candidate));
    }
    return result;
}

void populate_best(Result &result, const Lattice &lattice,
                   const RelationLattice &relation_lattice,
                   const Fixture &fixture,
                   const std::vector<Assignment> &assignments) {
    result.accepted_assignments = assignments.size();
    result.accepted_assignment_ids.reserve(assignments.size());
    for (const auto &assignment : assignments) {
        result.accepted_assignment_ids.push_back(assignment_id(assignment));
    }
    std::ranges::sort(result.accepted_assignment_ids);
    result.survivor_set_digest =
        survivor_digest(result.accepted_assignment_ids);
    if (assignments.empty()) {
        return;
    }
    std::vector<std::size_t> order(assignments.size());
    std::iota(order.begin(), order.end(), 0U);
    std::ranges::stable_sort(order, [&](const std::size_t left,
                                        const std::size_t right) {
        return assignment_score(lattice, assignments[left], &relation_lattice) >
               assignment_score(lattice, assignments[right], &relation_lattice);
    });
    const auto &best = assignments[order.front()];
    const auto best_assignment_score =
        assignment_score(lattice, best, &relation_lattice);
    result.best_score = assignment_score(lattice, best, &relation_lattice,
                                         &result.score_reasons);
    for (std::size_t token = 0; token < best.size(); ++token) {
        const auto &candidate = lattice.candidates[token][best[token]];
        result.best_analysis.push_back(AnalysisChoice{
            token,
            candidate.source_index,
            candidate.lemma,
            std::string{surface_part_name(candidate.part)},
            morphology_name(candidate),
        });
    }
    const bool dependency_strategy =
        result.strategy == Strategy::dependency_projection ||
        result.strategy == Strategy::dependency_attachment_search ||
        result.strategy == Strategy::dependency_tree_oracle ||
        result.strategy == Strategy::dependency_eisner ||
        result.strategy == Strategy::dependency_mst;
    if (dependency_strategy) {
        std::vector<const RelationCandidate *> selected_relations;
        result.best_relations = dependency_relations(
            lattice, relation_lattice, best,
            fixture.mode == GrammarMode::fragment, &selected_relations);
        result.relation_candidates_selected = selected_relations.size();
        for (const auto *relation : selected_relations) {
            result.best_relation_candidates.push_back(
                relation_candidate_choice(lattice, *relation));
        }
    }
    for (std::size_t rank = 0; rank < order.size(); ++rank) {
        const auto &assignment = assignments[order[rank]];
        if (!result.preferred_lemma_rank && result.preferred_lemmas_declared &&
            matches_preferred_lemmas(lattice, assignment, fixture)) {
            result.preferred_lemma_sequence_survives = true;
            result.preferred_lemma_rank = static_cast<std::uint64_t>(rank + 1U);
        }
        if (!result.morphology_gold_rank && result.morphology_gold_declared &&
            matches_morphology_gold(lattice, assignment, fixture)) {
            result.morphology_gold_survives = true;
            result.morphology_gold_rank = static_cast<std::uint64_t>(rank + 1U);
            result.morphology_gold_best_score_tie =
                std::abs(assignment_score(lattice, assignment,
                                          &relation_lattice) -
                         best_assignment_score) < 1.0e-9;
        }
        if (dependency_strategy && !result.dependency_gold_rank &&
            result.dependency_gold_declared &&
            matches_morphology_gold(lattice, assignment, fixture) &&
            matches_dependency_gold(
                dependency_relations(lattice, relation_lattice, assignment,
                                     fixture.mode == GrammarMode::fragment),
                fixture)) {
            result.dependency_gold_survives = true;
            result.dependency_gold_rank = static_cast<std::uint64_t>(rank + 1U);
            result.dependency_gold_best_score_tie =
                std::abs(assignment_score(lattice, assignment,
                                          &relation_lattice) -
                         best_assignment_score) < 1.0e-9;
        }
    }
}

void populate_tree_best(Result &result, const Lattice &lattice,
                        const RelationLattice &relation_lattice,
                        const Fixture &fixture,
                        const std::vector<DependencyTreeAnalysis> &trees) {
    if (trees.empty()) {
        return;
    }
    std::vector<std::size_t> order(trees.size());
    std::iota(order.begin(), order.end(), 0U);
    const auto combined_score = [&](const DependencyTreeAnalysis &tree) {
        return assignment_score(lattice, tree.assignment, &relation_lattice) +
               tree.arc_score;
    };
    std::ranges::stable_sort(order, [&](const auto left, const auto right) {
        const auto left_score = combined_score(trees[left]);
        const auto right_score = combined_score(trees[right]);
        if (left_score > right_score) {
            return true;
        }
        if (right_score > left_score) {
            return false;
        }
        return trees[left].id < trees[right].id;
    });

    const auto &best = trees[order.front()];
    const auto best_combined_score = combined_score(best);
    result.best_analysis.clear();
    result.best_relations.clear();
    result.best_relation_candidates.clear();
    result.relation_candidates_selected = 0U;
    result.score_reasons.clear();
    result.best_score = assignment_score(
        lattice, best.assignment, &relation_lattice, &result.score_reasons);
    result.best_tree_arc_score = best.arc_score;
    for (std::size_t token = 0; token < best.assignment.size(); ++token) {
        const auto &candidate =
            lattice.candidates[token][best.assignment[token]];
        result.best_analysis.push_back(AnalysisChoice{
            token,
            candidate.source_index,
            candidate.lemma,
            std::string{surface_part_name(candidate.part)},
            morphology_name(candidate),
        });
    }
    for (const auto &arc : best.arcs) {
        result.best_relations.push_back(arc.relation);
        result.score_reasons.push_back(ScoreReason{
            "T001", arc.score,
            std::to_string(arc.relation.dependent) + "->" +
                (arc.relation.head ? std::to_string(*arc.relation.head)
                                   : std::string{"root"}) +
                ":" + arc.relation.label + ":" + std::string{arc.reason}});
        *result.best_score += arc.score;
    }

    for (const auto &candidate : relation_lattice.candidates) {
        if (!relation_allowed(candidate) ||
            !relation_selected(candidate, best.assignment)) {
            continue;
        }
        const auto selected =
            std::ranges::any_of(best.arcs, [&](const DependencyArc &arc) {
                switch (std::to_underlying(candidate.kind)) {
                case std::to_underlying(
                    RelationCandidateKind::preposition_complement):
                    return arc.relation.dependent == candidate.governor.token &&
                           arc.relation.head == candidate.dependent.token &&
                           arc.relation.label == "case";
                case std::to_underlying(RelationCandidateKind::verb_argument):
                    return candidate.compatibility ==
                               Compatibility::compatible &&
                           arc.relation.dependent ==
                               candidate.dependent.token &&
                           arc.relation.head == candidate.governor.token &&
                           (arc.relation.label == "iobj" ||
                            arc.relation.label == "obl");
                case std::to_underlying(RelationCandidateKind::coordination):
                    return arc.relation.dependent ==
                               candidate.dependent.token &&
                           arc.relation.head == candidate.governor.token &&
                           arc.relation.label == "conj";
                case std::to_underlying(
                    RelationCandidateKind::comparison_standard):
                    return arc.relation.dependent ==
                               candidate.dependent.token &&
                           arc.relation.head == candidate.governor.token &&
                           arc.relation.label == "obl:cmp";
                default:
                    return false;
                }
            });
        if (!selected) {
            continue;
        }
        ++result.relation_candidates_selected;
        result.best_relation_candidates.push_back(
            relation_candidate_choice(lattice, candidate));
    }

    result.preferred_lemma_sequence_survives = false;
    result.preferred_lemma_rank.reset();
    result.morphology_gold_survives = false;
    result.morphology_gold_rank.reset();
    result.morphology_gold_best_score_tie.reset();
    if (result.dependency_gold_declared) {
        result.dependency_gold_survives = false;
        result.dependency_gold_rank.reset();
        result.dependency_gold_best_score_tie.reset();
    }
    for (std::size_t rank = 0; rank < order.size(); ++rank) {
        const auto &tree = trees[order[rank]];
        if (!result.preferred_lemma_rank && result.preferred_lemmas_declared &&
            matches_preferred_lemmas(lattice, tree.assignment, fixture)) {
            result.preferred_lemma_sequence_survives = true;
            result.preferred_lemma_rank = static_cast<std::uint64_t>(rank + 1U);
        }
        if (!result.morphology_gold_rank && result.morphology_gold_declared &&
            matches_morphology_gold(lattice, tree.assignment, fixture)) {
            result.morphology_gold_survives = true;
            result.morphology_gold_rank = static_cast<std::uint64_t>(rank + 1U);
            result.morphology_gold_best_score_tie =
                std::abs(combined_score(tree) - best_combined_score) < 1.0e-9;
        }
        std::vector<Relation> relations;
        relations.reserve(tree.arcs.size());
        for (const auto &arc : tree.arcs) {
            relations.push_back(arc.relation);
        }
        if (!result.dependency_gold_rank && result.dependency_gold_declared &&
            matches_morphology_gold(lattice, tree.assignment, fixture) &&
            matches_dependency_gold(relations, fixture)) {
            result.dependency_gold_survives = true;
            result.dependency_gold_rank = static_cast<std::uint64_t>(rank + 1U);
            result.dependency_gold_best_score_tie =
                std::abs(combined_score(tree) - best_combined_score) < 1.0e-9;
        }
    }
}

[[nodiscard]] std::vector<std::size_t> domain_counts(const Domains &domains) {
    std::vector<std::size_t> counts;
    counts.reserve(domains.size());
    for (const auto &domain : domains) {
        counts.push_back(active_count(domain));
    }
    return counts;
}

} // namespace

std::string_view strategy_name(const Strategy strategy) noexcept {
    switch (std::to_underlying(strategy)) {
    case std::to_underlying(Strategy::morphology):
        return "morphology";
    case std::to_underlying(Strategy::cartesian_leaf_check):
        return "cartesian-leaf-check";
    case std::to_underlying(Strategy::incremental_dfs):
        return "incremental-dfs";
    case std::to_underlying(Strategy::dfs_mrv_forward_checking):
        return "dfs-mrv-forward-checking";
    case std::to_underlying(Strategy::worklist_prefilter):
        return "worklist-prefilter";
    case std::to_underlying(Strategy::gac_propagation):
        return "gac-propagation";
    case std::to_underlying(Strategy::gac_residue_cache):
        return "gac-residue-cache";
    case std::to_underlying(Strategy::dependency_projection):
        return "dependency-projection";
    case std::to_underlying(Strategy::dependency_attachment_search):
        return "dependency-attachment-search";
    case std::to_underlying(Strategy::dependency_tree_oracle):
        return "dependency-tree-oracle";
    case std::to_underlying(Strategy::dependency_eisner):
        return "dependency-eisner";
    case std::to_underlying(Strategy::dependency_mst):
        return "dependency-mst";
    case std::to_underlying(Strategy::earley_fixed_point_recognizer):
        return "earley-fixed-point-recognizer";
    case std::to_underlying(Strategy::gslr_stackset_recognizer):
        return "gslr-stackset-recognizer";
    default:
        return "unknown";
    }
}

std::optional<Strategy> parse_strategy(const std::string_view value) {
    constexpr std::array strategies{Strategy::morphology,
                                    Strategy::cartesian_leaf_check,
                                    Strategy::incremental_dfs,
                                    Strategy::dfs_mrv_forward_checking,
                                    Strategy::worklist_prefilter,
                                    Strategy::gac_propagation,
                                    Strategy::gac_residue_cache,
                                    Strategy::dependency_projection,
                                    Strategy::dependency_attachment_search,
                                    Strategy::dependency_tree_oracle,
                                    Strategy::dependency_eisner,
                                    Strategy::dependency_mst,
                                    Strategy::earley_fixed_point_recognizer,
                                    Strategy::gslr_stackset_recognizer};
    const auto found = std::ranges::find_if(strategies, [&](const auto item) {
        return strategy_name(item) == value;
    });
    if (found == strategies.end()) {
        if (value == "cartesian") {
            return Strategy::cartesian_leaf_check;
        }
        if (value == "propagation") {
            return Strategy::worklist_prefilter;
        }
        if (value == "dependency") {
            return Strategy::dependency_projection;
        }
        if (value == "earley") {
            return Strategy::earley_fixed_point_recognizer;
        }
        if (value == "glr") {
            return Strategy::gslr_stackset_recognizer;
        }
        return std::nullopt;
    }
    return *found;
}

std::string_view grammar_mode_name(const GrammarMode mode) noexcept {
    return mode == GrammarMode::fragment ? "fragment" : "complete-clause";
}

std::vector<Token> tokenize(const std::string_view text) {
    std::vector<Token> result;
    std::size_t begin{};
    while (begin < text.size()) {
        while (begin < text.size() &&
               ascii_separator(static_cast<unsigned char>(text[begin]))) {
            ++begin;
        }
        if (begin == text.size()) {
            break;
        }
        std::size_t end = begin;
        while (end < text.size() &&
               !ascii_separator(static_cast<unsigned char>(text[end]))) {
            ++end;
        }
        const auto surface = std::string{text.substr(begin, end - begin)};
        result.push_back(Token{surface, surface, begin, end});
        begin = end;
    }
    return result;
}

std::vector<Fixture> load_corpus(const std::filesystem::path &path) {
    std::ifstream input{path};
    if (!input) {
        throw std::runtime_error{"cannot open corpus: " + path.string()};
    }
    std::vector<Fixture> fixtures;
    if (path.extension() == ".json") {
        const auto document = nlohmann::json::parse(input);
        if (document.value("schema", "") != "words-parser-fixtures" ||
            document.value("schemaVersion", 0U) != 2U) {
            throw std::runtime_error{"unsupported fixture schema: " +
                                     path.string()};
        }
        for (const auto &item : document.at("fixtures")) {
            Fixture fixture;
            fixture.id = item.at("id").get<std::string>();
            fixture.text = item.at("text").get<std::string>();
            fixture.phenomenon = item.at("phenomenon").get<std::string>();
            fixture.preferred_lemmas = item.value("preferredLemmaSequence",
                                                  std::vector<std::string>{});
            const auto mode = item.value("mode", "complete-clause");
            if (mode == "fragment") {
                fixture.mode = GrammarMode::fragment;
            } else if (mode != "complete-clause") {
                throw std::runtime_error{"invalid grammar mode in fixture " +
                                         fixture.id};
            }
            for (const auto &override :
                 item.value("lookupOverrides", nlohmann::json::array())) {
                fixture.lookup_overrides.push_back(LookupOverride{
                    override.at("token").get<std::size_t>(),
                    override.at("lookup").get<std::string>(),
                    override.at("reason").get<std::string>(),
                });
            }
            if (const auto annotation = item.find("annotation");
                annotation != item.end()) {
                const auto &source = annotation->at("source");
                const auto &evidence = annotation->at("evidence");
                fixture.annotation = FixtureAnnotation{
                    annotation->at("status").get<std::string>(),
                    FixtureSource{
                        source.at("catalogId").get<std::string>(),
                        source.at("title").get<std::string>(),
                        source.at("repository").get<std::string>(),
                        source.at("commit").get<std::string>(),
                        source.at("unitId").get<std::string>(),
                        source.at("blockId").get<std::string>(),
                        source.at("printedPage").get<std::string>(),
                        source.at("sourceText").get<std::string>(),
                    },
                    FixtureEvidence{
                        evidence.at("claimBlockId").get<std::string>(),
                        evidence.at("claim").get<std::string>(),
                        evidence.at("sourceAsserts")
                            .get<std::vector<std::string>>(),
                        evidence.at("editorialAdds")
                            .get<std::vector<std::string>>(),
                        evidence.at("reviewedOn").get<std::string>(),
                    },
                };
            }
            if (const auto gold = item.find("gold"); gold != item.end()) {
                GoldSpec parsed;
                for (const auto &token :
                     gold->value("morphology", nlohmann::json::array())) {
                    MorphologyGoldToken expected;
                    expected.token = token.at("token").get<std::size_t>();
                    for (const auto &alternative : token.at("alternatives")) {
                        MorphologyGoldAlternative value;
                        const auto optional_string =
                            [&](const std::string_view key,
                                std::optional<std::string> &target) {
                                if (const auto found = alternative.find(key);
                                    found != alternative.end()) {
                                    target = found->get<std::string>();
                                }
                            };
                        optional_string("lemma", value.lemma);
                        optional_string("part", value.part);
                        optional_string("case", value.grammatical_case);
                        optional_string("governs", value.governs_case);
                        optional_string("number", value.number);
                        optional_string("gender", value.gender);
                        optional_string("degree", value.degree);
                        optional_string("tense", value.tense);
                        optional_string("voice", value.voice);
                        optional_string("mood", value.mood);
                        if (const auto person = alternative.find("person");
                            person != alternative.end()) {
                            value.person = person->get<unsigned>();
                        }
                        expected.alternatives.push_back(std::move(value));
                    }
                    parsed.morphology.push_back(std::move(expected));
                }
                for (const auto &dependency :
                     gold->value("dependencies", nlohmann::json::array())) {
                    std::vector<Relation> relations;
                    for (const auto &relation : dependency.at("relations")) {
                        std::optional<std::size_t> head;
                        if (!relation.at("head").is_null()) {
                            head = relation.at("head").get<std::size_t>();
                        }
                        relations.push_back(Relation{
                            relation.at("dependent").get<std::size_t>(), head,
                            relation.at("label").get<std::string>()});
                    }
                    parsed.accepted_dependencies.push_back(
                        std::move(relations));
                }
                const auto token_count = tokenize(fixture.text).size();
                if (!parsed.morphology.empty()) {
                    if (parsed.morphology.size() != token_count) {
                        throw std::runtime_error{
                            "morphology gold must cover every token in " +
                            fixture.id};
                    }
                    std::vector<bool> seen(token_count);
                    for (const auto &token_gold : parsed.morphology) {
                        if (token_gold.token >= token_count ||
                            seen[token_gold.token] ||
                            token_gold.alternatives.empty()) {
                            throw std::runtime_error{
                                "invalid morphology gold coverage in " +
                                fixture.id};
                        }
                        seen[token_gold.token] = true;
                    }
                }
                for (const auto &relations : parsed.accepted_dependencies) {
                    if (relations.size() != token_count) {
                        throw std::runtime_error{
                            "dependency gold must cover every token in " +
                            fixture.id};
                    }
                    std::vector<bool> seen(token_count);
                    std::size_t roots{};
                    for (const auto &relation : relations) {
                        if (relation.dependent >= token_count ||
                            seen[relation.dependent] ||
                            (relation.head &&
                             (*relation.head >= token_count ||
                              *relation.head == relation.dependent))) {
                            throw std::runtime_error{
                                "invalid dependency gold coverage in " +
                                fixture.id};
                        }
                        seen[relation.dependent] = true;
                        roots += !relation.head;
                    }
                    if (roots != 1U) {
                        throw std::runtime_error{
                            "dependency gold must contain one root in " +
                            fixture.id};
                    }
                }
                fixture.gold = std::move(parsed);
            }
            fixtures.push_back(std::move(fixture));
        }
        if (fixtures.empty()) {
            throw std::runtime_error{"corpus is empty: " + path.string()};
        }
        return fixtures;
    }
    std::string line;
    std::size_t line_number{};
    while (std::getline(input, line)) {
        ++line_number;
        if (line.empty() || line.front() == '#') {
            continue;
        }
        const auto fields = split(line, '\t');
        if (fields.size() != 5U || (fields[1] != "0" && fields[1] != "1")) {
            throw std::runtime_error{"invalid corpus row " +
                                     std::to_string(line_number)};
        }
        fixtures.push_back(
            Fixture{fields[0], fields[4], fields[2], split(fields[3], '|'),
                    fields[1] == "1" ? GrammarMode::fragment
                                     : GrammarMode::complete_clause,
                    {},
                    std::nullopt,
                    std::nullopt});
    }
    if (fixtures.empty()) {
        throw std::runtime_error{"corpus is empty: " + path.string()};
    }
    return fixtures;
}

Experiment::Experiment(const words::Engine &engine,
                       const std::uint64_t max_product)
    : engine_{engine}, max_product_{max_product} {}

Result Experiment::run(const Fixture &fixture, const Strategy strategy) const {
    const auto started = Clock::now();
    Result result;
    result.fixture_id = fixture.id;
    result.text = fixture.text;
    result.strategy = strategy;
    result.dataset_id = std::string{engine_.dataset_id()};
    result.source_commit = PARSERS_INVESTIGATION_GIT_COMMIT;
    result.compiler = PARSERS_INVESTIGATION_COMPILER;
    result.compiler_version = PARSERS_INVESTIGATION_COMPILER_VERSION;
    result.build_type = PARSERS_INVESTIGATION_BUILD_TYPE;
    result.max_product = max_product_;
    result.phenomenon = fixture.phenomenon;
    result.grammar_mode = fixture.mode;
    result.fixture_annotation = fixture.annotation;
    result.preferred_lemmas_declared = !fixture.preferred_lemmas.empty();
    result.morphology_gold_declared =
        fixture.gold && !fixture.gold->morphology.empty();
    result.dependency_gold_declared =
        fixture.gold && !fixture.gold->accepted_dependencies.empty();
    if ((strategy == Strategy::dependency_projection ||
         strategy == Strategy::dependency_attachment_search ||
         strategy == Strategy::dependency_tree_oracle ||
         strategy == Strategy::dependency_eisner ||
         strategy == Strategy::dependency_mst) &&
        result.dependency_gold_declared) {
        result.dependency_gold_survives = false;
    }

    const auto lattice =
        build_lattice(engine_, fixture.text, fixture.lookup_overrides);
    result.token_count = lattice.tokens.size();
    result.lookup_overrides = fixture.lookup_overrides;
    result.surface_tokens.reserve(lattice.tokens.size());
    result.lookup_tokens.reserve(lattice.tokens.size());
    for (const auto &token : lattice.tokens) {
        result.surface_tokens.push_back(token.surface);
        result.lookup_tokens.push_back(token.lookup);
    }
    result.candidate_counts.reserve(lattice.candidates.size());
    bool contains_empty_domain{};
    for (std::size_t token = 0; token < lattice.candidates.size(); ++token) {
        const auto count = lattice.candidates[token].size();
        result.candidate_counts.push_back(count);
        if (count == 0U) {
            contains_empty_domain = true;
            result.diagnostics.push_back("unknown-token:" +
                                         lattice.tokens[token].surface);
        }
    }
    const auto product = raw_product(lattice);
    result.raw_product = product.convert_to<std::string>();
    result.domains_after_propagation = result.candidate_counts;
    result.peak_bytes = static_cast<std::uint64_t>(
        sizeof(Lattice) + lattice.tokens.size() * sizeof(Token));
    for (const auto &domain : lattice.candidates) {
        result.peak_bytes +=
            static_cast<std::uint64_t>(domain.size() * sizeof(Candidate));
    }

    if (strategy == Strategy::morphology) {
        if (contains_empty_domain) {
            result.status = "no-analysis";
            result.rejections["H001"] = 1U;
        }
        if (result.preferred_lemmas_declared &&
            fixture.preferred_lemmas.size() == lattice.candidates.size()) {
            result.preferred_lemma_sequence_survives = true;
            for (std::size_t token = 0; token < lattice.candidates.size();
                 ++token) {
                const bool found = std::ranges::any_of(
                    lattice.candidates[token], [&](const Candidate &candidate) {
                        return candidate.lemma ==
                               fixture.preferred_lemmas[token];
                    });
                result.preferred_lemma_sequence_survives =
                    result.preferred_lemma_sequence_survives && found;
            }
        }
        if (result.morphology_gold_declared) {
            result.morphology_gold_survives =
                morphology_gold_in_lattice(lattice, fixture);
        }
        result.elapsed_ns = static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() -
                                                                 started)
                .count());
        return result;
    }

    if (contains_empty_domain) {
        result.status = "no-analysis";
        result.rejections["H001"] = 1U;
        result.elapsed_ns = static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() -
                                                                 started)
                .count());
        return result;
    }
    const auto relation_lattice = build_relation_lattice(lattice);
    result.relation_candidate_generation_performed = true;
    result.relation_candidates_generated = relation_lattice.candidates.size();
    result.relation_candidates_by_kind = relation_lattice.by_kind;
    result.relation_candidates_by_compatibility =
        relation_lattice.by_compatibility;
    result.peak_bytes += static_cast<std::uint64_t>(
        relation_lattice.candidates.size() * sizeof(RelationCandidate));
    const bool uses_residues =
        strategy == Strategy::gac_residue_cache ||
        strategy == Strategy::dependency_projection ||
        strategy == Strategy::dependency_attachment_search ||
        strategy == Strategy::dependency_tree_oracle ||
        strategy == Strategy::dependency_eisner ||
        strategy == Strategy::dependency_mst ||
        strategy == Strategy::earley_fixed_point_recognizer ||
        strategy == Strategy::gslr_stackset_recognizer;
    const bool uses_gac =
        strategy == Strategy::gac_propagation || uses_residues;
    const bool prefiltered =
        strategy == Strategy::worklist_prefilter || uses_gac;
    if (!prefiltered && product > max_product_) {
        result.status = "experiment-budget-exceeded";
        result.diagnostics.push_back("raw-product-exceeds-max-product");
        result.elapsed_ns = static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() -
                                                                 started)
                .count());
        return result;
    }

    Domains domains = full_domains(lattice);
    if (prefiltered) {
        if (uses_gac) {
            domains = gac_propagate(
                lattice, relation_lattice,
                fixture.mode == GrammarMode::fragment, result.removals,
                result.propagation_support_checks,
                result.propagation_queue_pops, result.propagation_revisions,
                uses_residues, result.propagation_residue_hits,
                result.propagation_residue_misses,
                result.propagation_residue_invalidations,
                result.propagation_residue_candidate_checks);
        } else {
            domains =
                propagate(lattice, fixture.mode == GrammarMode::fragment,
                          result.removals, result.propagation_support_checks,
                          result.propagation_iterations);
        }
        result.domains_after_propagation = domain_counts(domains);
        const auto pruned = domain_product(domains);
        result.pruned_product = pruned.convert_to<std::string>();
        if (pruned > max_product_) {
            result.status = "experiment-budget-exceeded";
            result.diagnostics.push_back("pruned-product-exceeds-max-product");
            result.elapsed_ns = static_cast<std::uint64_t>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(
                    Clock::now() - started)
                    .count());
            return result;
        }
    }
    Enumeration enumeration;
    const bool fragment = fixture.mode == GrammarMode::fragment;
    if (strategy == Strategy::incremental_dfs) {
        enumeration =
            enumerate_incremental(lattice, relation_lattice, domains, fragment);
    } else if (strategy == Strategy::dfs_mrv_forward_checking) {
        enumeration = enumerate_mrv_forward_checking(lattice, relation_lattice,
                                                     domains, fragment);
    } else {
        enumeration = enumerate(lattice, relation_lattice, domains, fragment);
    }
    result.enumeration_constraint_checks = enumeration.checks;
    result.enumeration_partial_states = enumeration.states;
    result.enumeration_backtracks = enumeration.backtracks;
    result.complete_assignments = enumeration.complete;
    result.rejections = std::move(enumeration.rejections);

    std::vector<Assignment> accepted;
    std::vector<DependencyTreeAnalysis> dependency_trees;
    if (strategy == Strategy::cartesian_leaf_check ||
        strategy == Strategy::incremental_dfs ||
        strategy == Strategy::dfs_mrv_forward_checking ||
        strategy == Strategy::worklist_prefilter ||
        strategy == Strategy::gac_propagation ||
        strategy == Strategy::gac_residue_cache) {
        accepted = enumeration.valid;
    } else if (strategy == Strategy::dependency_projection) {
        for (const auto &assignment : enumeration.valid) {
            const auto relations =
                dependency_relations(lattice, relation_lattice, assignment,
                                     fixture.mode == GrammarMode::fragment);
            if (!relations.empty()) {
                accepted.push_back(assignment);
                result.dependency_relations_emitted += relations.size();
            }
        }
    } else if (strategy == Strategy::dependency_attachment_search) {
        result.attachment_search_performed = true;
        bool attachment_budget_exceeded{};
        for (const auto &assignment : enumeration.valid) {
            const auto remaining =
                max_product_ - result.attachment_complete_analyses;
            auto attachments = enumerate_attachments(lattice, relation_lattice,
                                                     assignment, remaining);
            result.attachment_slots_created += attachments.slots;
            result.attachment_partial_states += attachments.partial_states;
            result.attachment_conflicts += attachments.conflicts;
            result.attachment_complete_analyses += attachments.analyses.size();
            if (attachments.budget_exceeded) {
                attachment_budget_exceeded = true;
                break;
            }
            if (attachments.analyses.empty()) {
                continue;
            }
            accepted.push_back(assignment);
            const auto projected = projected_attachment_indices(
                lattice, relation_lattice, assignment, fragment);
            ++result.projected_analyses_checked;
            bool projection_found{};
            for (auto &analysis : attachments.analyses) {
                std::ranges::sort(analysis);
                result.attachment_analysis_ids.push_back(
                    attachment_analysis_id(assignment, analysis));
                projection_found = projection_found || analysis == projected;
            }
            result.projected_analyses_in_search += projection_found;
            const auto relations = dependency_relations(
                lattice, relation_lattice, assignment, fragment);
            result.dependency_relations_emitted += relations.size();
        }
        if (attachment_budget_exceeded) {
            result.status = "experiment-budget-exceeded";
            result.diagnostics.push_back(
                "attachment-analyses-exceed-max-product");
            result.attachment_analysis_ids.clear();
            result.elapsed_ns = static_cast<std::uint64_t>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(
                    Clock::now() - started)
                    .count());
            return result;
        }
        std::ranges::sort(result.attachment_analysis_ids);
        result.attachment_set_digest =
            survivor_digest(result.attachment_analysis_ids);
        if (result.projected_analyses_in_search !=
            result.projected_analyses_checked) {
            result.diagnostics.push_back(
                "deterministic-projection-not-in-attachment-search");
        }
    } else if (strategy == Strategy::dependency_tree_oracle) {
        result.tree_search_performed = true;
        bool tree_budget_exceeded{};
        for (const auto &assignment : enumeration.valid) {
            const auto remaining = max_product_ - result.tree_complete_analyses;
            auto trees = enumerate_dependency_trees(
                lattice, relation_lattice, assignment, fragment, remaining);
            result.tree_arc_candidates_generated += trees.arc_candidates;
            result.tree_partial_states += trees.partial_states;
            result.tree_cycle_rejections += trees.cycle_rejections;
            result.tree_root_rejections += trees.root_rejections;
            result.tree_complete_analyses += trees.analyses.size();
            if (trees.budget_exceeded) {
                tree_budget_exceeded = true;
                break;
            }
            if (trees.analyses.empty()) {
                continue;
            }
            accepted.push_back(assignment);
            const auto projection_id = projected_tree_id(
                lattice, relation_lattice, assignment, fragment);
            ++result.projected_trees_checked;
            bool projection_found{};
            std::optional<double> best_projective;
            std::optional<double> best_unrestricted;
            for (auto &tree : trees.analyses) {
                projection_found = projection_found || tree.id == projection_id;
                result.tree_analysis_ids.push_back(tree.id);
                if (!best_unrestricted || tree.arc_score > *best_unrestricted) {
                    best_unrestricted = tree.arc_score;
                }
                if (tree.projective) {
                    ++result.tree_projective_analyses;
                    if (!best_projective || tree.arc_score > *best_projective) {
                        best_projective = tree.arc_score;
                    }
                } else {
                    ++result.tree_nonprojective_analyses;
                }
                result.dependency_relations_emitted += tree.arcs.size();
                dependency_trees.push_back(std::move(tree));
            }
            const auto id = assignment_id(assignment);
            if (best_projective) {
                result.tree_best_projective_scores[id] = *best_projective;
            }
            if (best_unrestricted) {
                result.tree_best_unrestricted_scores[id] = *best_unrestricted;
            }
            result.projected_trees_in_search += projection_found;
        }
        if (tree_budget_exceeded) {
            result.status = "experiment-budget-exceeded";
            result.diagnostics.push_back("dependency-trees-exceed-max-product");
            result.tree_analysis_ids.clear();
            result.elapsed_ns = static_cast<std::uint64_t>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(
                    Clock::now() - started)
                    .count());
            return result;
        }
        std::ranges::sort(result.tree_analysis_ids);
        result.tree_set_digest = survivor_digest(result.tree_analysis_ids);
        if (result.projected_trees_in_search !=
            result.projected_trees_checked) {
            result.diagnostics.push_back(
                "deterministic-projection-not-in-tree-search");
        }
    } else if (strategy == Strategy::dependency_eisner ||
               strategy == Strategy::dependency_mst) {
        result.decoder_performed = true;
        for (const auto &assignment : enumeration.valid) {
            auto decoded = strategy == Strategy::dependency_eisner
                               ? decode_eisner(lattice, relation_lattice,
                                               assignment, fragment)
                               : decode_mst(lattice, relation_lattice,
                                            assignment, fragment);
            result.decoder_arc_candidates += decoded.arc_candidates;
            result.decoder_states += decoded.states;
            result.decoder_cycles_contracted += decoded.cycles_contracted;
            if (!decoded.tree) {
                continue;
            }
            accepted.push_back(assignment);
            ++result.decoder_complete_analyses;
            if (decoded.tree->projective) {
                ++result.decoder_projective_analyses;
            } else {
                ++result.decoder_nonprojective_analyses;
            }
            result.decoder_scores[assignment_id(assignment)] =
                decoded.tree->arc_score;
            result.decoder_analysis_ids.push_back(decoded.tree->id);
            result.dependency_relations_emitted += decoded.tree->arcs.size();
            dependency_trees.push_back(std::move(*decoded.tree));
        }
        std::ranges::sort(result.decoder_analysis_ids);
        result.decoder_set_digest =
            survivor_digest(result.decoder_analysis_ids);
    } else {
        for (const auto &assignment : enumeration.valid) {
            const auto choice = resolve(lattice, assignment);
            const auto input = categories(choice);
            bool recognized{};
            if (strategy == Strategy::earley_fixed_point_recognizer) {
                recognized = earley_accepts(input, fixture.mode,
                                            result.parser_units_created,
                                            result.parser_duplicate_deductions,
                                            result.parser_live_units);
            } else {
                recognized = glr_accepts(input, fixture.mode,
                                         result.parser_units_created,
                                         result.parser_duplicate_deductions,
                                         result.parser_live_units);
            }
            if (recognized) {
                accepted.push_back(assignment);
            }
        }
    }
    if (accepted.empty()) {
        result.status = "no-parse";
    }
    populate_best(result, lattice, relation_lattice, fixture, accepted);
    if (strategy == Strategy::dependency_tree_oracle ||
        strategy == Strategy::dependency_eisner ||
        strategy == Strategy::dependency_mst) {
        populate_tree_best(result, lattice, relation_lattice, fixture,
                           dependency_trees);
    }
    result.peak_bytes += static_cast<std::uint64_t>(
        enumeration.valid.size() * lattice.tokens.size() * sizeof(std::size_t));
    result.peak_bytes += static_cast<std::uint64_t>(dependency_trees.size() *
                                                    lattice.tokens.size() *
                                                    sizeof(DependencyArc));
    result.elapsed_ns = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() -
                                                             started)
            .count());
    return result;
}

bool Experiment::self_test(const std::vector<Fixture> &fixtures,
                           std::string &failure) const {
    const auto punctuation = tokenize("Arma, virumque cano!");
    if (punctuation.size() != 3U || punctuation[0].surface != "Arma" ||
        punctuation[2].surface != "cano") {
        failure = "tokenizer did not preserve the expected three words";
        return false;
    }
    {
        std::uint64_t created{};
        std::uint64_t reused{};
        std::uint64_t live{};
        const std::vector<Category> nominal_fragment{Category::nominal};
        if (earley_accepts(nominal_fragment, GrammarMode::complete_clause,
                           created, reused, live)) {
            failure = "complete-clause grammar admitted a verbless fragment";
            return false;
        }
        created = reused = live = 0U;
        if (!earley_accepts(nominal_fragment, GrammarMode::fragment, created,
                            reused, live)) {
            failure = "fragment grammar rejected a nominal fragment";
            return false;
        }
    }
    {
        const Fixture budget_fixture{
            "budget-after-propagation",   "Veni",
            "finite-domain-pruning",      {},
            GrammarMode::complete_clause, {}, std::nullopt, std::nullopt};
        const Experiment tiny_budget{engine_, 4U};
        const auto cartesian =
            tiny_budget.run(budget_fixture, Strategy::cartesian_leaf_check);
        const auto worklist =
            tiny_budget.run(budget_fixture, Strategy::worklist_prefilter);
        const auto gac =
            tiny_budget.run(budget_fixture, Strategy::gac_propagation);
        const auto cached =
            tiny_budget.run(budget_fixture, Strategy::gac_residue_cache);
        if (cartesian.status != "experiment-budget-exceeded" ||
            worklist.status != "ok" || worklist.raw_product != "6" ||
            worklist.pruned_product != "3" || gac.status != "ok" ||
            gac.pruned_product != "3" || cached.status != "ok" ||
            cached.pruned_product != "3") {
            failure = "propagation was not allowed to run before its budget";
            return false;
        }
        if (cached.propagation_residue_hits == 0U ||
            cached.propagation_support_checks >=
                gac.propagation_support_checks) {
            failure = "GAC residue cache did not reuse the finite support";
            return false;
        }
    }
    for (const auto &fixture : fixtures) {
        const auto morphology = run(fixture, Strategy::morphology);
        if (morphology.status != "ok") {
            failure = fixture.id + ": morphology has an unknown token";
            return false;
        }
        if (fixture.annotation.has_value() !=
                morphology.fixture_annotation.has_value() ||
            (fixture.annotation &&
             fixture.annotation->source.catalog_id !=
                 morphology.fixture_annotation->source.catalog_id)) {
            failure = fixture.id + ": fixture provenance was not preserved";
            return false;
        }
        if (morphology.surface_tokens.size() !=
                morphology.lookup_tokens.size() ||
            morphology.lookup_overrides != fixture.lookup_overrides) {
            failure = fixture.id + ": lookup metadata was not preserved";
            return false;
        }
        for (const auto &override : fixture.lookup_overrides) {
            if (override.token >= morphology.surface_tokens.size() ||
                morphology.lookup_tokens[override.token] != override.lookup ||
                morphology.surface_tokens[override.token] == override.lookup) {
                failure = fixture.id +
                          ": lookup override mutated or failed to replace the "
                          "source spelling";
                return false;
            }
        }
        if (morphology.preferred_lemmas_declared &&
            !morphology.preferred_lemma_sequence_survives) {
            failure = fixture.id + ": preferred lemmas are absent from lattice";
            return false;
        }
        if (morphology.morphology_gold_declared &&
            !morphology.morphology_gold_survives) {
            failure = fixture.id + ": structured morphology gold is absent";
            return false;
        }
        const auto cartesian = run(fixture, Strategy::cartesian_leaf_check);
        const auto propagation_result =
            run(fixture, Strategy::worklist_prefilter);
        if (cartesian.status != "ok" || propagation_result.status != "ok") {
            failure = fixture.id + ": exact strategy did not produce a parse";
            return false;
        }
        if (cartesian.accepted_assignment_ids !=
            propagation_result.accepted_assignment_ids) {
            failure = fixture.id +
                      ": propagation differs extensionally from cartesian";
            return false;
        }
        const auto incremental = run(fixture, Strategy::incremental_dfs);
        const auto mrv = run(fixture, Strategy::dfs_mrv_forward_checking);
        const auto gac = run(fixture, Strategy::gac_propagation);
        const auto cached = run(fixture, Strategy::gac_residue_cache);
        if (incremental.status != "ok" || mrv.status != "ok" ||
            gac.status != "ok" || cached.status != "ok" ||
            incremental.accepted_assignment_ids !=
                cartesian.accepted_assignment_ids ||
            mrv.accepted_assignment_ids != cartesian.accepted_assignment_ids ||
            gac.accepted_assignment_ids != cartesian.accepted_assignment_ids ||
            cached.accepted_assignment_ids !=
                cartesian.accepted_assignment_ids) {
            failure = fixture.id +
                      ": Track A strategy differs extensionally from cartesian";
            return false;
        }
        if (fixture.phenomenon.starts_with("H002-") &&
            !gac.removals.contains("H002")) {
            failure = fixture.id + ": GAC did not expose H002 pruning";
            return false;
        }
        if (fixture.phenomenon.starts_with("H005-") &&
            !gac.removals.contains("H005")) {
            failure = fixture.id + ": GAC did not expose H005 pruning";
            return false;
        }
        if (fixture.phenomenon.starts_with("H007-") &&
            !gac.removals.contains("H007")) {
            failure = fixture.id + ": GAC did not expose H007 pruning";
            return false;
        }
        if (fixture.id == "preposition-in-ablative" &&
            (propagation_result.pruned_product != "6" ||
             gac.pruned_product != "2")) {
            failure = fixture.id + ": expected stronger bidirectional GAC";
            return false;
        }
        if (cartesian.morphology_gold_declared &&
            (!cartesian.morphology_gold_survives ||
             !propagation_result.morphology_gold_survives)) {
            failure = fixture.id + ": preferred analysis was pruned";
            return false;
        }
        const auto dependency = run(fixture, Strategy::dependency_projection);
        if (dependency.status != "ok" ||
            dependency.accepted_assignment_ids !=
                propagation_result.accepted_assignment_ids ||
            !dependency.best_score || dependency.score_reasons.empty()) {
            failure =
                fixture.id + ": dependency result is incomplete or unexplained";
            return false;
        }
        if (dependency.dependency_gold_declared &&
            (!dependency.dependency_gold_survives ||
             !*dependency.dependency_gold_survives)) {
            failure = fixture.id + ": dependency gold was not projected";
            return false;
        }
        const auto attachment =
            run(fixture, Strategy::dependency_attachment_search);
        if (attachment.status != "ok" ||
            attachment.accepted_assignment_ids !=
                dependency.accepted_assignment_ids ||
            !attachment.attachment_search_performed ||
            attachment.attachment_analysis_ids.empty() ||
            attachment.attachment_set_digest.empty() ||
            attachment.projected_analyses_checked !=
                attachment.accepted_assignments ||
            attachment.projected_analyses_in_search !=
                attachment.projected_analyses_checked) {
            failure = fixture.id +
                      ": attachment search lost an assignment or projection";
            return false;
        }
        if (attachment.dependency_gold_declared &&
            (!attachment.dependency_gold_survives ||
             !*attachment.dependency_gold_survives)) {
            failure = fixture.id +
                      ": attachment search did not preserve dependency gold";
            return false;
        }
        if (fixture.id == "governed-dative-argument" &&
            (attachment.accepted_assignments != 7U ||
             attachment.attachment_complete_analyses != 8U)) {
            failure = fixture.id +
                      ": optional governed attachment was not enumerated";
            return false;
        }
        if (fixture.id == "governed-dative-argument") {
            const Experiment attachment_budget{engine_, 7U};
            const auto capped = attachment_budget.run(
                fixture, Strategy::dependency_attachment_search);
            if (capped.status != "experiment-budget-exceeded" ||
                std::ranges::find(capped.diagnostics,
                                  "attachment-analyses-exceed-max-product") ==
                    capped.diagnostics.end()) {
                failure = fixture.id +
                          ": attachment enumeration ignored its safety budget";
                return false;
            }
        }
        const auto tree = run(fixture, Strategy::dependency_tree_oracle);
        if (tree.status != "ok" ||
            tree.accepted_assignment_ids !=
                dependency.accepted_assignment_ids ||
            !tree.tree_search_performed || tree.tree_analysis_ids.empty() ||
            tree.tree_set_digest.empty() ||
            tree.tree_complete_analyses < tree.accepted_assignments ||
            tree.tree_projective_analyses + tree.tree_nonprojective_analyses !=
                tree.tree_complete_analyses ||
            tree.projected_trees_checked != tree.accepted_assignments ||
            tree.projected_trees_in_search != tree.projected_trees_checked) {
            failure = fixture.id +
                      ": exact tree oracle violated a structural invariant";
            return false;
        }
        if (tree.dependency_gold_declared &&
            (!tree.dependency_gold_survives ||
             !*tree.dependency_gold_survives)) {
            failure = fixture.id + ": dependency gold is absent from tree set";
            return false;
        }
        const auto roots = std::ranges::count_if(
            tree.best_relations,
            [](const Relation &relation) { return !relation.head; });
        if (tree.best_relations.size() != tree.token_count || roots != 1) {
            failure = fixture.id + ": top tree is incomplete or multi-rooted";
            return false;
        }
        const double explained_tree_score = std::accumulate(
            tree.score_reasons.begin(), tree.score_reasons.end(), 0.0,
            [](const double total, const ScoreReason &reason) {
                return total + reason.delta;
            });
        if (!tree.best_score ||
            std::abs(explained_tree_score - *tree.best_score) > 1.0e-9) {
            failure = fixture.id + ": tree score is not fully explained";
            return false;
        }
        if (fixture.id == "nonprojective-hyperbaton") {
            if (tree.tree_nonprojective_analyses == 0U ||
                tree.dependency_gold_rank != 1U) {
                failure =
                    fixture.id + ": crossing gold tree was not ranked first";
                return false;
            }
            const Experiment tree_budget{engine_, 100U};
            const auto capped =
                tree_budget.run(fixture, Strategy::dependency_tree_oracle);
            if (capped.status != "experiment-budget-exceeded" ||
                std::ranges::find(capped.diagnostics,
                                  "dependency-trees-exceed-max-product") ==
                    capped.diagnostics.end()) {
                failure =
                    fixture.id + ": tree enumeration ignored its safety budget";
                return false;
            }
        }
        const auto eisner = run(fixture, Strategy::dependency_eisner);
        const auto mst = run(fixture, Strategy::dependency_mst);
        const auto decoder_matches_oracle =
            [&](const Result &decoder,
                const std::map<std::string, double, std::less<>> &expected) {
                if (decoder.status != "ok" || !decoder.decoder_performed ||
                    decoder.accepted_assignment_ids !=
                        tree.accepted_assignment_ids ||
                    decoder.decoder_complete_analyses !=
                        decoder.accepted_assignments ||
                    decoder.decoder_scores.size() != expected.size()) {
                    return false;
                }
                for (const auto &[assignment, score] : expected) {
                    const auto found = decoder.decoder_scores.find(assignment);
                    if (found == decoder.decoder_scores.end() ||
                        std::abs(found->second - score) > 1.0e-9) {
                        return false;
                    }
                }
                return std::ranges::all_of(
                    decoder.decoder_analysis_ids, [&](const std::string &id) {
                        return std::ranges::binary_search(
                            tree.tree_analysis_ids, id);
                    });
            };
        if (!decoder_matches_oracle(eisner, tree.tree_best_projective_scores) ||
            !decoder_matches_oracle(mst, tree.tree_best_unrestricted_scores) ||
            eisner.decoder_nonprojective_analyses != 0U ||
            eisner.decoder_projective_analyses !=
                eisner.decoder_complete_analyses) {
            failure =
                fixture.id + ": dependency decoder differs from exact optimum";
            return false;
        }
        if (fixture.id == "nonprojective-hyperbaton") {
            if ((eisner.dependency_gold_survives &&
                 *eisner.dependency_gold_survives) ||
                !mst.dependency_gold_survives ||
                !*mst.dependency_gold_survives ||
                mst.dependency_gold_rank != 1U ||
                mst.decoder_nonprojective_analyses == 0U) {
                failure = fixture.id +
                          ": projective and unrestricted decoders did not "
                          "separate";
                return false;
            }
        } else if ((eisner.dependency_gold_declared &&
                    (!eisner.dependency_gold_survives ||
                     !*eisner.dependency_gold_survives)) ||
                   (mst.dependency_gold_declared &&
                    (!mst.dependency_gold_survives ||
                     !*mst.dependency_gold_survives))) {
            failure = fixture.id + ": decoder lost projective dependency gold";
            return false;
        }
        if (fixture.id == "agreement-feminine-plural") {
            const auto tied_second = [](const Result &value) {
                return value.morphology_gold_rank == 2U &&
                       value.morphology_gold_best_score_tie &&
                       *value.morphology_gold_best_score_tie;
            };
            const auto dependency_tied_second = [](const Result &value) {
                return value.dependency_gold_rank == 2U &&
                       value.dependency_gold_best_score_tie &&
                       *value.dependency_gold_best_score_tie;
            };
            if (!tied_second(cartesian) || !tied_second(dependency) ||
                !tied_second(tree) || !tied_second(eisner) ||
                !tied_second(mst) || !dependency_tied_second(dependency) ||
                !dependency_tied_second(tree) ||
                !dependency_tied_second(eisner) ||
                !dependency_tied_second(mst)) {
                failure = fixture.id +
                          ": didactic adjective should tie the homographic "
                          "participle at the best score";
                return false;
            }
        }
        if (fixture.id == "agreement-neuter-plural" &&
            mst.decoder_cycles_contracted == 0U) {
            failure = fixture.id +
                      ": Chu-Liu/Edmonds cycle contraction was not exercised";
            return false;
        }
        const auto selected_relation_kind =
            [&](const std::string_view constraint) {
                return std::ranges::any_of(
                    dependency.best_relation_candidates,
                    [&](const RelationCandidateChoice &relation) {
                        return relation.constraint_id == constraint;
                    });
            };
        if (fixture.phenomenon.starts_with("H005-") &&
            !selected_relation_kind("H005")) {
            failure =
                fixture.id + ": projection did not select an H005 attachment";
            return false;
        }
        if (fixture.phenomenon.starts_with("H007-") &&
            !selected_relation_kind("H007")) {
            failure =
                fixture.id + ": projection did not select an H007 attachment";
            return false;
        }
        if (fixture.phenomenon.starts_with("H011-")) {
            const auto has_label = [&](const std::string_view label) {
                return std::ranges::any_of(
                    dependency.best_relations, [&](const Relation &relation) {
                        return relation.label == label;
                    });
            };
            const auto comparison = std::ranges::find_if(
                dependency.best_relation_candidates,
                [](const RelationCandidateChoice &relation) {
                    return relation.constraint_id == "H011";
                });
            const bool uses_quam = fixture.phenomenon.ends_with("-quam");
            if (comparison == dependency.best_relation_candidates.end() ||
                comparison->contexts.size() != (uses_quam ? 2U : 0U) ||
                !has_label("obl:cmp") ||
                (fixture.phenomenon.ends_with("-quam") &&
                 !has_label("mark"))) {
                failure = fixture.id +
                          ": comparative relation was not projected completely";
                return false;
            }
        }
        if (fixture.phenomenon == "H006-candidate-government") {
            const bool scored_government = std::ranges::any_of(
                dependency.score_reasons,
                [](const ScoreReason &reason) { return reason.id == "S008"; });
            const auto incompatible =
                dependency.relation_candidates_by_compatibility.find(
                    "incompatible");
            if (!selected_relation_kind("H006") || !scored_government ||
                incompatible ==
                    dependency.relation_candidates_by_compatibility.end() ||
                incompatible->second == 0U) {
                failure =
                    fixture.id + ": governed argument edge was not exercised";
                return false;
            }
        }
        if (fixture.id == "governed-complement-not-mandatory" &&
            selected_relation_kind("H006")) {
            failure = fixture.id + ": optional argument became mandatory";
            return false;
        }
        const double explained_score = std::accumulate(
            dependency.score_reasons.begin(), dependency.score_reasons.end(),
            0.0, [](const double total, const ScoreReason &reason) {
                return total + reason.delta;
            });
        if (std::abs(explained_score - *dependency.best_score) > 1.0e-9) {
            failure = fixture.id + ": score reasons do not sum to bestScore";
            return false;
        }
        const auto earley =
            run(fixture, Strategy::earley_fixed_point_recognizer);
        const auto glr = run(fixture, Strategy::gslr_stackset_recognizer);
        if (earley.status != glr.status ||
            earley.accepted_assignment_ids != glr.accepted_assignment_ids) {
            failure = fixture.id + ": Earley and GLR recognize different sets";
            return false;
        }
        if (earley.status != "ok" || (earley.morphology_gold_declared &&
                                      (!earley.morphology_gold_survives ||
                                       !glr.morphology_gold_survives))) {
            failure = fixture.id + ": chart parser lost the preferred analysis";
            return false;
        }

        // Ensure structured gold detects a morphology error even when the
        // lemma is unchanged, and a relation error even when morphology is
        // correct. This tests the evaluator rather than lexical coverage.
        if (fixture.gold && !dependency.best_analysis.empty()) {
            const auto lattice =
                build_lattice(engine_, fixture.text, fixture.lookup_overrides);
            Assignment best(lattice.candidates.size());
            for (const auto &choice : dependency.best_analysis) {
                best[choice.token] = choice.candidate;
            }
            auto wrong_morphology = fixture;
            if (!wrong_morphology.gold->morphology.empty() &&
                !wrong_morphology.gold->morphology.front()
                     .alternatives.empty()) {
                wrong_morphology.gold->morphology.front()
                    .alternatives.front()
                    .grammatical_case = "not-a-case";
                if (matches_morphology_gold(lattice, best, wrong_morphology)) {
                    failure =
                        fixture.id + ": structured gold ignored a wrong case";
                    return false;
                }
            }
            auto wrong_dependency = fixture;
            if (!wrong_dependency.gold->accepted_dependencies.empty() &&
                !wrong_dependency.gold->accepted_dependencies.front().empty()) {
                wrong_dependency.gold->accepted_dependencies.front()
                    .front()
                    .label = "not-a-relation";
                if (matches_dependency_gold(dependency.best_relations,
                                            wrong_dependency)) {
                    failure = fixture.id +
                              ": structured gold ignored a wrong relation";
                    return false;
                }
            }
        }
    }
    return true;
}

std::string to_json(const Result &result) {
    using Json = nlohmann::ordered_json;
    Json output{
        {"schema", result.schema},
        {"schemaVersion", result.schema_version},
        {"fixtureId", result.fixture_id},
        {"text", result.text},
        {"strategy", strategy_name(result.strategy)},
        {"datasetId", result.dataset_id},
        {"sourceCommit", result.source_commit},
        {"compiler", result.compiler},
        {"compilerVersion", result.compiler_version},
        {"buildType", result.build_type},
        {"maxProduct", result.max_product},
        {"status", result.status},
        {"phenomenon", result.phenomenon},
        {"grammarMode", grammar_mode_name(result.grammar_mode)},
        {"elapsedNs", result.elapsed_ns},
        {"peakBytes", result.peak_bytes},
        {"peakBytesKind", "estimated-owned-structures"},
        {"diagnostics", result.diagnostics},
    };
    if (result.fixture_annotation) {
        const auto &annotation = *result.fixture_annotation;
        output["fixtureAnnotation"] = {
            {"status", annotation.status},
            {"source",
             {{"catalogId", annotation.source.catalog_id},
              {"title", annotation.source.title},
              {"repository", annotation.source.repository},
              {"commit", annotation.source.commit},
              {"unitId", annotation.source.unit_id},
              {"blockId", annotation.source.block_id},
              {"printedPage", annotation.source.printed_page},
              {"sourceText", annotation.source.source_text}}},
            {"evidence",
             {{"claimBlockId", annotation.evidence.claim_block_id},
              {"claim", annotation.evidence.claim},
              {"sourceAsserts", annotation.evidence.source_asserts},
              {"editorialAdds", annotation.evidence.editorial_adds},
              {"reviewedOn", annotation.evidence.reviewed_on}}},
        };
    } else {
        output["fixtureAnnotation"] = nullptr;
    }
    output["morphology"] = {
        {"tokenCount", result.token_count},
        {"surfaceTokens", result.surface_tokens},
        {"lookupTokens", result.lookup_tokens},
        {"lookupOverrides", Json::array()},
        {"candidateCounts", result.candidate_counts},
        {"rawProduct", result.raw_product},
    };
    for (const auto &override : result.lookup_overrides) {
        output["morphology"]["lookupOverrides"].push_back(
            {{"token", override.token},
             {"lookup", override.lookup},
             {"reason", override.reason}});
    }
    output["propagation"] = {
        {"performed",
         result.strategy == Strategy::worklist_prefilter ||
             result.strategy == Strategy::gac_propagation ||
             result.strategy == Strategy::gac_residue_cache ||
             result.strategy == Strategy::dependency_projection ||
             result.strategy == Strategy::dependency_attachment_search ||
             result.strategy == Strategy::dependency_tree_oracle ||
             result.strategy == Strategy::dependency_eisner ||
             result.strategy == Strategy::dependency_mst ||
             result.strategy == Strategy::earley_fixed_point_recognizer ||
             result.strategy == Strategy::gslr_stackset_recognizer},
        {"algorithm",
         result.strategy == Strategy::worklist_prefilter
             ? Json("fixed-point-scan")
             : (result.strategy == Strategy::gac_propagation
                    ? Json("gac-agenda")
                    : (result.strategy == Strategy::gac_residue_cache ||
                               result.strategy ==
                                   Strategy::dependency_projection ||
                               result.strategy ==
                                   Strategy::dependency_attachment_search ||
                               result.strategy ==
                                   Strategy::dependency_tree_oracle ||
                               result.strategy == Strategy::dependency_eisner ||
                               result.strategy == Strategy::dependency_mst ||
                               result.strategy ==
                                   Strategy::earley_fixed_point_recognizer ||
                               result.strategy ==
                                   Strategy::gslr_stackset_recognizer
                           ? Json("gac-agenda-residues")
                           : Json(nullptr)))},
        {"iterations", result.propagation_iterations},
        {"queuePops", result.propagation_queue_pops},
        {"revisions", result.propagation_revisions},
        {"supportChecks", result.propagation_support_checks},
        {"residueHits", result.propagation_residue_hits},
        {"residueMisses", result.propagation_residue_misses},
        {"residueInvalidations", result.propagation_residue_invalidations},
        {"residueCandidateChecks", result.propagation_residue_candidate_checks},
        {"removalsByConstraint", result.removals},
        {"domainsAfter", result.domains_after_propagation},
        {"prunedProduct", result.pruned_product.empty()
                              ? Json(nullptr)
                              : Json(result.pruned_product)},
    };
    output["enumeration"] = {
        {"performed", result.strategy != Strategy::morphology},
        {"partialStates", result.enumeration_partial_states},
        {"constraintChecks", result.enumeration_constraint_checks},
        {"backtracks", result.enumeration_backtracks},
        {"completeAssignments", result.complete_assignments},
        {"rejectionsByConstraint", result.rejections},
    };
    output["relationCandidates"] = {
        {"generationPerformed", result.relation_candidate_generation_performed},
        {"generated", result.relation_candidates_generated},
        {"byKind", result.relation_candidates_by_kind},
        {"byCompatibility", result.relation_candidates_by_compatibility},
        {"selected", result.relation_candidates_selected},
        {"best", Json::array()},
    };
    for (const auto &relation : result.best_relation_candidates) {
        output["relationCandidates"]["best"].push_back(
            {{"kind", relation.kind},
             {"governor", relation.governor},
             {"governorCandidate", relation.governor_candidate},
             {"dependent", relation.dependent},
             {"dependentCandidate", relation.dependent_candidate},
             {"contexts", Json::array()},
             {"constraint", relation.constraint_id},
             {"compatibility", relation.compatibility}});
        auto &contexts = output["relationCandidates"]["best"].back()["contexts"];
        for (const auto &context : relation.contexts) {
            contexts.push_back(
                {{"token", context.token}, {"candidate", context.candidate}});
        }
    }
    output["attachmentSearch"] = {
        {"performed", result.attachment_search_performed},
        {"slotsCreated", result.attachment_slots_created},
        {"partialStates", result.attachment_partial_states},
        {"completeAnalyses", result.attachment_complete_analyses},
        {"conflicts", result.attachment_conflicts},
        {"analysisIds", result.attachment_analysis_ids},
        {"analysisSetDigest",
         result.attachment_set_digest.empty()
             ? Json(nullptr)
             : Json{{"algorithm", "fnv1a-64"},
                    {"value", result.attachment_set_digest}}},
        {"projectionChecked", result.projected_analyses_checked},
        {"projectionInSearch", result.projected_analyses_in_search},
    };
    output["treeSearch"] = {
        {"performed", result.tree_search_performed},
        {"arcCandidatesGenerated", result.tree_arc_candidates_generated},
        {"partialStates", result.tree_partial_states},
        {"completeTrees", result.tree_complete_analyses},
        {"projectiveTrees", result.tree_projective_analyses},
        {"nonprojectiveTrees", result.tree_nonprojective_analyses},
        {"cycleRejections", result.tree_cycle_rejections},
        {"rootRejections", result.tree_root_rejections},
        {"analysisIds", result.tree_analysis_ids},
        {"analysisSetDigest", result.tree_set_digest.empty()
                                  ? Json(nullptr)
                                  : Json{{"algorithm", "fnv1a-64"},
                                         {"value", result.tree_set_digest}}},
        {"projectionChecked", result.projected_trees_checked},
        {"projectionInSearch", result.projected_trees_in_search},
        {"bestArcScore", result.best_tree_arc_score
                             ? Json(*result.best_tree_arc_score)
                             : Json(nullptr)},
        {"bestProjectiveScores", result.tree_best_projective_scores},
        {"bestUnrestrictedScores", result.tree_best_unrestricted_scores},
    };
    output["decoder"] = {
        {"performed", result.decoder_performed},
        {"algorithm", result.strategy == Strategy::dependency_eisner
                          ? Json("eisner-projective")
                          : (result.strategy == Strategy::dependency_mst
                                 ? Json("chu-liu-edmonds")
                                 : Json(nullptr))},
        {"arcCandidates", result.decoder_arc_candidates},
        {"states", result.decoder_states},
        {"cyclesContracted", result.decoder_cycles_contracted},
        {"completeTrees", result.decoder_complete_analyses},
        {"projectiveTrees", result.decoder_projective_analyses},
        {"nonprojectiveTrees", result.decoder_nonprojective_analyses},
        {"analysisIds", result.decoder_analysis_ids},
        {"analysisSetDigest", result.decoder_set_digest.empty()
                                  ? Json(nullptr)
                                  : Json{{"algorithm", "fnv1a-64"},
                                         {"value", result.decoder_set_digest}}},
        {"scoresByAssignment", result.decoder_scores},
    };
    output["parser"] = {
        {"kind", strategy_name(result.strategy)},
        {"recognizerOnly",
         result.strategy == Strategy::earley_fixed_point_recognizer ||
             result.strategy == Strategy::gslr_stackset_recognizer},
        {"unitsCreated", result.parser_units_created},
        {"duplicateDeductions", result.parser_duplicate_deductions},
        {"liveUnitsObserved", result.parser_live_units},
        {"dependencyRelationsEmitted", result.dependency_relations_emitted},
    };
    output["forest"] = {
        {"available", false},
        {"derivationCount", nullptr},
        {"sppfNodes", nullptr},
        {"packedAlternatives", nullptr},
        {"reason", "strategies-do-not-build-a-shared-packed-forest"},
    };
    output["acceptance"] = {
        {"morphAssignments", result.accepted_assignments},
        {"assignmentIds", result.accepted_assignment_ids},
        {"survivorSetDigest",
         result.survivor_set_digest.empty()
             ? Json(nullptr)
             : Json{{"algorithm", "fnv1a-64"},
                    {"value", result.survivor_set_digest}}},
    };
    output["gold"] = {
        {"preferredLemmaSequence",
         {{"declared", result.preferred_lemmas_declared},
          {"survives", result.preferred_lemma_sequence_survives},
          {"rank", result.preferred_lemma_rank
                       ? Json(*result.preferred_lemma_rank)
                       : Json(nullptr)}}},
        {"morphology",
         {{"declared", result.morphology_gold_declared},
          {"survives", result.morphology_gold_survives},
          {"rank", result.morphology_gold_rank
                       ? Json(*result.morphology_gold_rank)
                       : Json(nullptr)},
          {"bestScoreTie", result.morphology_gold_best_score_tie
                               ? Json(*result.morphology_gold_best_score_tie)
                               : Json(nullptr)}}},
        {"dependency",
         {{"declared", result.dependency_gold_declared},
          {"evaluated",
           result.strategy == Strategy::dependency_projection ||
               result.strategy == Strategy::dependency_attachment_search ||
               result.strategy == Strategy::dependency_tree_oracle ||
               result.strategy == Strategy::dependency_eisner ||
               result.strategy == Strategy::dependency_mst},
          {"survives", result.dependency_gold_survives
                           ? Json(*result.dependency_gold_survives)
                           : Json(nullptr)},
          {"rank", result.dependency_gold_rank
                       ? Json(*result.dependency_gold_rank)
                       : Json(nullptr)},
          {"bestScoreTie", result.dependency_gold_best_score_tie
                               ? Json(*result.dependency_gold_best_score_tie)
                               : Json(nullptr)}}},
    };
    output["bestScore"] =
        result.best_score ? Json(*result.best_score) : Json(nullptr);
    output["scoreReasons"] = Json::array();
    for (const auto &reason : result.score_reasons) {
        output["scoreReasons"].push_back({{"id", reason.id},
                                          {"delta", reason.delta},
                                          {"detail", reason.detail}});
    }
    output["bestAnalysis"] = Json::array();
    for (const auto &choice : result.best_analysis) {
        output["bestAnalysis"].push_back({{"token", choice.token},
                                          {"candidate", choice.candidate},
                                          {"lemma", choice.lemma},
                                          {"part", choice.part},
                                          {"morphology", choice.morphology}});
    }
    output["bestRelations"] = Json::array();
    for (const auto &relation : result.best_relations) {
        output["bestRelations"].push_back(
            {{"dependent", relation.dependent},
             {"head", relation.head ? Json(*relation.head) : Json(nullptr)},
             {"label", relation.label}});
    }
    return output.dump();
}

} // namespace parsers
