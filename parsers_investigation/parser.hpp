#pragma once

#include "words/engine.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace parsers {

enum class Strategy {
    morphology,
    cartesian_leaf_check,
    incremental_dfs,
    dfs_mrv_forward_checking,
    worklist_prefilter,
    gac_propagation,
    gac_residue_cache,
    dependency_projection,
    dependency_attachment_search,
    dependency_tree_oracle,
    dependency_eisner,
    dependency_mst,
    earley_fixed_point_recognizer,
    gslr_stackset_recognizer,
};

enum class GrammarMode { complete_clause, fragment };

struct Token final {
    std::string surface;
    std::string lookup;
    std::size_t byte_begin{};
    std::size_t byte_end{};
};

struct LookupOverride final {
    std::size_t token{};
    std::string lookup;
    std::string reason;
    bool operator==(const LookupOverride &) const = default;
};

struct Relation final {
    std::size_t dependent{};
    std::optional<std::size_t> head;
    std::string label;
    bool operator==(const Relation &) const = default;
};

struct MorphologyGoldAlternative final {
    std::optional<std::string> lemma;
    std::optional<std::string> part;
    std::optional<std::string> grammatical_case;
    std::optional<std::string> governs_case;
    std::optional<std::string> number;
    std::optional<std::string> gender;
    std::optional<std::string> degree;
    std::optional<std::string> tense;
    std::optional<std::string> voice;
    std::optional<std::string> mood;
    std::optional<unsigned> person;
};

struct MorphologyGoldToken final {
    std::size_t token{};
    std::vector<MorphologyGoldAlternative> alternatives;
};

struct GoldSpec final {
    std::vector<MorphologyGoldToken> morphology;
    std::vector<std::vector<Relation>> accepted_dependencies;
};

struct FixtureSource final {
    std::string catalog_id;
    std::string title;
    std::string repository;
    std::string commit;
    std::string unit_id;
    std::string block_id;
    std::string printed_page;
    std::string source_text;
};

struct FixtureEvidence final {
    std::string claim_block_id;
    std::string claim;
    std::vector<std::string> source_asserts;
    std::vector<std::string> editorial_adds;
    std::string reviewed_on;
};

struct FixtureAnnotation final {
    std::string status;
    FixtureSource source;
    FixtureEvidence evidence;
};

struct Fixture final {
    std::string id;
    std::string text;
    std::string phenomenon;
    std::vector<std::string> preferred_lemmas;
    GrammarMode mode{GrammarMode::complete_clause};
    std::vector<LookupOverride> lookup_overrides;
    std::optional<FixtureAnnotation> annotation;
    std::optional<GoldSpec> gold;
};

struct AnalysisChoice final {
    std::size_t token{};
    std::size_t candidate{};
    std::string lemma;
    std::string part;
    std::string morphology;
};

struct ScoreReason final {
    std::string id;
    double delta{};
    std::string detail;
};

struct RelationCandidateContextChoice final {
    std::size_t token{};
    std::size_t candidate{};
};

struct RelationCandidateChoice final {
    std::string kind;
    std::size_t governor{};
    std::size_t governor_candidate{};
    std::size_t dependent{};
    std::size_t dependent_candidate{};
    std::vector<RelationCandidateContextChoice> contexts;
    std::string constraint_id;
    std::string compatibility;
};

// Schema v2 keeps unlike units separate. The counters below are internal
// storage for those namespaces; to_json() is the normative wire contract.
struct Result final {
    std::string schema{"words-parser-investigation"};
    std::uint32_t schema_version{2};
    std::string fixture_id;
    std::string text;
    Strategy strategy{Strategy::morphology};
    GrammarMode grammar_mode{GrammarMode::complete_clause};
    std::string dataset_id;
    std::string source_commit;
    std::string compiler;
    std::string compiler_version;
    std::string build_type;
    std::uint64_t max_product{};
    std::string status{"ok"};
    std::string phenomenon;
    std::vector<std::string> surface_tokens;
    std::vector<std::string> lookup_tokens;
    std::vector<LookupOverride> lookup_overrides;

    std::size_t token_count{};
    std::vector<std::size_t> candidate_counts;
    std::string raw_product;

    std::vector<std::size_t> domains_after_propagation;
    std::string pruned_product;
    std::uint64_t propagation_iterations{};
    std::uint64_t propagation_queue_pops{};
    std::uint64_t propagation_revisions{};
    std::uint64_t propagation_support_checks{};
    std::uint64_t propagation_residue_hits{};
    std::uint64_t propagation_residue_misses{};
    std::uint64_t propagation_residue_invalidations{};
    std::uint64_t propagation_residue_candidate_checks{};
    std::map<std::string, std::uint64_t, std::less<>> removals;

    bool relation_candidate_generation_performed{};
    std::uint64_t relation_candidates_generated{};
    std::map<std::string, std::uint64_t, std::less<>>
        relation_candidates_by_kind;
    std::map<std::string, std::uint64_t, std::less<>>
        relation_candidates_by_compatibility;
    std::uint64_t relation_candidates_selected{};
    std::vector<RelationCandidateChoice> best_relation_candidates;

    bool attachment_search_performed{};
    std::uint64_t attachment_slots_created{};
    std::uint64_t attachment_partial_states{};
    std::uint64_t attachment_complete_analyses{};
    std::uint64_t attachment_conflicts{};
    std::uint64_t projected_analyses_checked{};
    std::uint64_t projected_analyses_in_search{};
    std::vector<std::string> attachment_analysis_ids;
    std::string attachment_set_digest;

    bool tree_search_performed{};
    std::uint64_t tree_arc_candidates_generated{};
    std::uint64_t tree_partial_states{};
    std::uint64_t tree_complete_analyses{};
    std::uint64_t tree_projective_analyses{};
    std::uint64_t tree_nonprojective_analyses{};
    std::uint64_t tree_cycle_rejections{};
    std::uint64_t tree_root_rejections{};
    std::uint64_t projected_trees_checked{};
    std::uint64_t projected_trees_in_search{};
    std::vector<std::string> tree_analysis_ids;
    std::string tree_set_digest;
    std::optional<double> best_tree_arc_score;
    std::map<std::string, double, std::less<>> tree_best_projective_scores;
    std::map<std::string, double, std::less<>> tree_best_unrestricted_scores;

    bool decoder_performed{};
    std::uint64_t decoder_arc_candidates{};
    std::uint64_t decoder_states{};
    std::uint64_t decoder_cycles_contracted{};
    std::uint64_t decoder_complete_analyses{};
    std::uint64_t decoder_projective_analyses{};
    std::uint64_t decoder_nonprojective_analyses{};
    std::vector<std::string> decoder_analysis_ids;
    std::string decoder_set_digest;
    std::map<std::string, double, std::less<>> decoder_scores;

    std::uint64_t enumeration_constraint_checks{};
    std::uint64_t enumeration_partial_states{};
    std::uint64_t enumeration_backtracks{};
    std::uint64_t complete_assignments{};
    std::map<std::string, std::uint64_t, std::less<>> rejections;

    std::uint64_t parser_units_created{};
    std::uint64_t parser_duplicate_deductions{};
    std::uint64_t parser_live_units{};
    std::uint64_t dependency_relations_emitted{};

    std::uint64_t accepted_assignments{};
    std::vector<std::string> accepted_assignment_ids;
    std::string survivor_set_digest;

    std::uint64_t elapsed_ns{};
    std::uint64_t peak_bytes{};

    bool preferred_lemmas_declared{};
    bool preferred_lemma_sequence_survives{};
    std::optional<std::uint64_t> preferred_lemma_rank;
    bool morphology_gold_declared{};
    bool morphology_gold_survives{};
    std::optional<std::uint64_t> morphology_gold_rank;
    std::optional<bool> morphology_gold_best_score_tie;
    bool dependency_gold_declared{};
    std::optional<bool> dependency_gold_survives;
    std::optional<std::uint64_t> dependency_gold_rank;
    std::optional<bool> dependency_gold_best_score_tie;
    std::optional<FixtureAnnotation> fixture_annotation;

    std::optional<double> best_score;
    std::vector<ScoreReason> score_reasons;
    std::vector<AnalysisChoice> best_analysis;
    std::vector<Relation> best_relations;
    std::vector<std::string> diagnostics;
};

[[nodiscard]] std::string_view strategy_name(Strategy strategy) noexcept;
[[nodiscard]] std::optional<Strategy> parse_strategy(std::string_view value);
[[nodiscard]] std::string_view grammar_mode_name(GrammarMode mode) noexcept;
[[nodiscard]] std::vector<Token> tokenize(std::string_view text);
[[nodiscard]] std::vector<Fixture>
load_corpus(const std::filesystem::path &path);

class Experiment final {
  public:
    Experiment(const words::Engine &engine, std::uint64_t max_product);

    [[nodiscard]] Result run(const Fixture &fixture, Strategy strategy) const;
    [[nodiscard]] bool self_test(const std::vector<Fixture> &fixtures,
                                 std::string &failure) const;

  private:
    const words::Engine &engine_;
    std::uint64_t max_product_{};
};

[[nodiscard]] std::string to_json(const Result &result);

} // namespace parsers
