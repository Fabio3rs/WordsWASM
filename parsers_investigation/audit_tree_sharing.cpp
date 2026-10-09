// Standalone local census. Build instructions and limitations: TREE_SHARING.md.
#include "parser.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <tuple>
#include <vector>

namespace {
using Json = nlohmann::ordered_json;

bool same_choice(const parsers::AnalysisChoice &a,
                 const parsers::AnalysisChoice &b) {
    const auto fields = [](const auto &v) {
        return std::tie(v.token, v.candidate, v.lemma, v.part, v.morphology,
                        v.features, v.derivation, v.generated_by_whitaker,
                        v.whitaker_trim_compatible, v.whitaker_trim_reasons,
                        v.morphological_notices, v.span_role, v.span_partner);
    };
    return fields(a) == fields(b);
}

// Account for this ABI's string buffers outside the string object (SSO excluded).
// These are requested capacities, not allocator overhead, RSS, or a peak.
std::size_t string_bytes(const std::string &value) {
    const auto object = reinterpret_cast<std::uintptr_t>(&value);
    const auto data = reinterpret_cast<std::uintptr_t>(value.data());
    return data >= object && data < object + sizeof(value)
               ? 0U : value.capacity() + 1U;
}

std::size_t strings_bytes(const std::vector<std::string> &values) {
    auto bytes = values.capacity() * sizeof(std::string);
    for (const auto &value : values) bytes += string_bytes(value);
    return bytes;
}

std::size_t choice_nested_bytes(const parsers::AnalysisChoice &v) {
    return string_bytes(v.lemma) + string_bytes(v.part) +
           string_bytes(v.morphology) + string_bytes(v.derivation) +
           string_bytes(v.span_role) + strings_bytes(v.whitaker_trim_reasons) +
           strings_bytes(v.morphological_notices);
}

std::size_t analysis_bytes(const std::vector<parsers::AnalysisChoice> &values) {
    auto bytes = values.capacity() * sizeof(parsers::AnalysisChoice);
    for (const auto &value : values) bytes += choice_nested_bytes(value);
    return bytes;
}

using ArcKey = std::tuple<std::size_t, std::optional<std::size_t>, std::string>;
std::vector<ArcKey> arc_key(const std::vector<parsers::Relation> &relations) {
    std::vector<ArcKey> key;
    for (const auto &r : relations) key.emplace_back(r.dependent, r.head, r.label);
    std::ranges::sort(key);
    return key;
}

Json census(const parsers::Result &result) {
    std::map<std::string, const parsers::RankedMorphologyAnalysis *> assignments;
    std::map<std::pair<std::size_t, std::size_t>, const parsers::AnalysisChoice *> choices;
    const auto collect = [&](const auto &analysis) {
        for (const auto &choice : analysis) {
            const auto [it, inserted] = choices.emplace(
                std::pair{choice.token, choice.candidate}, &choice);
            if (!inserted && !same_choice(choice, *it->second))
                throw std::runtime_error("candidate ID maps to different AnalysisChoice");
        }
    };
    std::size_t morphology_slots{}, morphology_buffer{}, morphology_owned{};
    for (const auto &m : result.morphology_nbest) {
        if (!assignments.emplace(m.assignment_id, &m).second)
            throw std::runtime_error("duplicate morphology assignment");
        morphology_slots += m.analysis.size();
        morphology_buffer += m.analysis.capacity() * sizeof(parsers::AnalysisChoice);
        morphology_owned += analysis_bytes(m.analysis);
        collect(m.analysis);
    }
    std::set<std::string> tree_ids;
    std::set<std::pair<std::string, std::vector<ArcKey>>> identities;
    std::set<std::vector<ArcKey>> topologies;
    std::map<std::string, std::size_t> multiplicities;
    std::size_t tree_slots{}, relation_buffer{};
    std::size_t projection_matches{}, choice_checks{}, metadata_checks{};
    std::size_t tree_assignment_id_bytes{}, tree_id_bytes{};
    std::size_t relation_label_bytes{};
    for (const auto &tree : result.tree_nbest) {
        const auto found = assignments.find(tree.assignment_id);
        if (found == assignments.end())
            throw std::runtime_error("tree assignment missing from morphology N-best");
        const auto &m = *found->second;
        const auto view = parsers::dependency_tree_view(result, tree);
        if (view.analysis.data() != m.analysis.data() ||
            view.analysis.size() != m.analysis.size() ||
            !std::equal(view.analysis.begin(), view.analysis.end(),
                        m.analysis.begin(), same_choice))
            throw std::runtime_error("tree morphology differs from assignment");
        choice_checks += view.analysis.size();
        if (tree.morphology_score != m.manual_score ||
            tree.matches_morphology_gold != m.matches_morphology_gold ||
            tree.matches_preferred_lemmas != m.matches_preferred_lemmas)
            throw std::runtime_error("tree morphology metadata differs");
        ++metadata_checks;
        const auto arcs = arc_key(tree.relations);
        tree_ids.insert(tree.tree_id);
        identities.emplace(tree.assignment_id, arcs);
        topologies.insert(arcs);
        ++multiplicities[tree.assignment_id];
        projection_matches += arcs == arc_key(m.relations);
        tree_slots += view.analysis.size();
        relation_buffer += tree.relations.capacity() * sizeof(parsers::Relation);
        for (const auto &relation : tree.relations)
            relation_label_bytes += string_bytes(relation.label);
        tree_assignment_id_bytes += string_bytes(tree.assignment_id);
        tree_id_bytes += string_bytes(tree.tree_id);
        collect(view.analysis);
    }
    collect(result.best_analysis);
    std::map<std::size_t, std::size_t> histogram;
    std::size_t max_trees{}, unique_choice_nested{};
    for (const auto &[id, count] : multiplicities) {
        static_cast<void>(id);
        ++histogram[count];
        max_trees = std::max(max_trees, count);
    }
    for (const auto &[id, choice] : choices) {
        static_cast<void>(id);
        unique_choice_nested += choice_nested_bytes(*choice);
    }
    Json distribution = Json::array();
    for (const auto &[trees, count] : histogram)
        distribution.push_back({{"treesPerAssignment", trees}, {"assignments", count}});
    return {
        {"fixture", result.fixture_id}, {"status", result.status},
        {"assignments", assignments.size()}, {"trees", result.tree_nbest.size()},
        {"uniqueTreeIds", tree_ids.size()}, {"uniqueAssignmentAndArcs", identities.size()},
        {"uniqueArcTopologies", topologies.size()},
        {"assignmentsReferencedByTrees", multiplicities.size()},
        {"maxTreesPerAssignment", max_trees}, {"multiplicity", distribution},
        {"treesMatchingMorphologyProjection", projection_matches},
        {"treeChoiceEqualityChecks", choice_checks},
        {"treeMorphologyMetadataEqualityChecks", metadata_checks},
        {"uniqueSelectedTokenCandidates", choices.size()},
        {"morphologyChoiceSlots", morphology_slots}, {"treeChoiceSlots", tree_slots},
        {"morphologyAnalysisBufferBytes", morphology_buffer},
        {"morphologyAnalysisOwnedBytes", morphology_owned},
        {"treeAnalysisBufferBytes", 0}, {"treeAnalysisOwnedBytes", 0},
        {"treeMorphologyIndexBytes", result.tree_nbest.size() * sizeof(parsers::MorphologyIndex)},
        {"treeRelationBufferBytes", relation_buffer},
        {"treeRelationLabelBytes", relation_label_bytes},
        {"treeVectorBufferBytes", result.tree_nbest.capacity() * sizeof(parsers::RankedDependencyTreeAnalysis)},
        {"treeAssignmentIdExternalBytes", tree_assignment_id_bytes},
        {"treeIdExternalBytes", tree_id_bytes},
        {"bestAnalysisOwnedBytes", analysis_bytes(result.best_analysis)},
        {"hypotheticalPackedCandidatePoolBytes", choices.size() * sizeof(parsers::AnalysisChoice) + unique_choice_nested},
    };
}
} // namespace

int main(int argc, char **argv) try {
    if (argc != 4) {
        std::cerr << "usage: audit_tree_sharing DATABASE CORPUS STRATEGY\n";
        return 2;
    }
    std::ifstream file(argv[1], std::ios::binary | std::ios::ate);
    if (!file || file.tellg() <= 0) throw std::runtime_error("cannot read database");
    std::vector<std::byte> bytes(static_cast<std::size_t>(file.tellg()));
    file.seekg(0);
    file.read(reinterpret_cast<char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!file) throw std::runtime_error("incomplete database read");
    auto engine = words::Engine::create(std::move(bytes), words::EngineConfig{});
    if (!engine) throw std::runtime_error(engine.error().message);
    const auto strategy = parsers::parse_strategy(argv[3]);
    if (!strategy) throw std::runtime_error("unknown strategy");
    const parsers::Experiment experiment{**engine, 1'000'000U};
    Json output{
        {"schema", "words-parser-tree-sharing-audit"}, {"schemaVersion", 2},
        {"treeMorphologyRepresentation", "index-into-morphology-nbest"},
        {"strategy", argv[3]},
        {"sizes", {{"AnalysisChoice", sizeof(parsers::AnalysisChoice)},
                   {"Relation", sizeof(parsers::Relation)},
                   {"RankedMorphologyAnalysis", sizeof(parsers::RankedMorphologyAnalysis)},
                   {"RankedDependencyTreeAnalysis", sizeof(parsers::RankedDependencyTreeAnalysis)},
                   {"size_t", sizeof(std::size_t)}}},
        {"fixtures", Json::array()},
    };
    for (const auto &fixture : parsers::load_corpus(argv[2])) {
        const auto result = experiment.run(fixture, *strategy);
        if (result.status != "ok" && result.status != "approximate")
            throw std::runtime_error("unexpected parse status: " + result.status);
        output["fixtures"].push_back(census(result));
    }
    std::cout << output.dump(2) << '\n';
} catch (const std::exception &error) {
    std::cerr << "audit_tree_sharing: " << error.what() << '\n';
    return 1;
}
