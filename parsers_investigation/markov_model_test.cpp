#include "dependency_markov.hpp"
#include "markov_features.hpp"
#include "markov_model.hpp"

#include <cmath>
#include <iostream>
#include <ranges>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

[[nodiscard]] bool near(const double left, const double right) {
    return std::abs(left - right) < 1.0e-12;
}

void require(const bool condition, const std::string_view message) {
    if (!condition) {
        throw std::runtime_error{std::string{message}};
    }
}

} // namespace

int main() {
    parsers::MarkovModel model{1U, 0.5};
    model.train({"NOM", "VERB"});
    model.train({"NOM", "VERB"});
    model.train({"ACC", "VERB"});

    require(model.order() == 1U, "order");
    require(near(model.alpha(), 0.5), "alpha");
    require(near(model.training_weight(), 3.0), "training weight");
    require(model.vocabulary_size() == 5U, "vocabulary");
    require(model.log_probability({"NOM", "VERB"}) >
                model.log_probability({"ACC", "VERB"}),
            "known frequency ranking");
    const auto additive_unseen = model.score({"UNSEEN", "VERB"});
    require(std::isfinite(additive_unseen.log_probability),
            "smoothing for unseen state");
    require(additive_unseen.diagnostics.unknown_states == 1U &&
                additive_unseen.diagnostics.uniform_fallbacks != 0U,
            "additive unknown and uniform-fallback diagnostics");

    parsers::MarkovModel weighted{2U};
    weighted.train({"noun", "verb"}, 0.5);
    weighted.train({"noun", "adjective"}, 0.5);
    require(near(weighted.training_weight(), 1.0), "fractional weights");

    parsers::MarkovModel backed_off{
        2U, 0.1, parsers::MarkovSmoothing::hierarchical_backoff, 1.0};
    backed_off.train({"A", "X"});
    backed_off.train({"A", "X"});
    backed_off.train({"A", "X"});
    backed_off.train({"B", "Y"});
    const auto backed_off_x = backed_off.score({"UNSEEN", "X"});
    const auto backed_off_y = backed_off.score({"UNSEEN", "Y"});
    require(backed_off_x.log_probability > backed_off_y.log_probability,
            "backoff must reuse the more frequent shorter-context outcome");
    require(backed_off_x.diagnostics.unknown_states == 1U,
            "unknown state diagnostics");
    require(backed_off_x.diagnostics.full_context_misses != 0U &&
                backed_off_x.diagnostics.backed_off_transitions != 0U,
            "backoff diagnostics");
    require(backed_off_x.diagnostics.uniform_fallbacks == 0U,
            "hierarchical backoff must not use uniform missing-row fallback");

    bool rejected{};
    try {
        parsers::MarkovModel invalid{0U};
        static_cast<void>(invalid);
    } catch (const std::invalid_argument &) {
        rejected = true;
    }
    require(rejected, "zero order rejection");

    rejected = false;
    try {
        parsers::MarkovModel invalid_backoff{
            1U, 0.1, parsers::MarkovSmoothing::hierarchical_backoff, 0.0};
        static_cast<void>(invalid_backoff);
    } catch (const std::invalid_argument &) {
        rejected = true;
    }
    require(rejected, "zero backoff strength rejection");

    rejected = false;
    try {
        parsers::MarkovModel empty{1U};
        static_cast<void>(empty.log_probability({"noun"}));
    } catch (const std::logic_error &) {
        rejected = true;
    }
    require(rejected, "untrained model rejection");

    const auto make_token =
        [](const std::size_t token, const std::string &lemma,
           const std::string &part, const std::string &morphology) {
            return parsers::AnalysisChoice{token, token,      lemma,
                                           part,  morphology, {}};
        };
    const parsers::RankedMorphologyAnalysis svo{
        "svo",
        0.0,
        {make_token(0U, "puella", "noun", "nominative-singular-feminine"),
         make_token(1U, "poeta", "noun", "accusative-singular-masculine"),
         make_token(2U, "amo", "verb", "present-active-indicative-3-singular")},
        {{0U, 2U, "nsubj"}, {1U, 2U, "obj"}, {2U, std::nullopt, "root"}},
        true,
        true};
    const parsers::RankedMorphologyAnalysis vos{
        "vos",
        0.0,
        {make_token(0U, "amo", "verb", "present-active-indicative-3-singular"),
         make_token(1U, "poeta", "noun", "accusative-singular-masculine"),
         make_token(2U, "puella", "noun", "nominative-singular-feminine")},
        {{0U, std::nullopt, "root"}, {1U, 0U, "obj"}, {2U, 0U, "nsubj"}},
        true,
        true};
    const auto canonical_signature = [](const auto &analysis,
                                        const auto traversal) {
        std::vector<std::string> signature;
        for (const auto &entry : parsers::markov::parser_canonical_order(
                 analysis,
                 [&](const auto token) {
                     return analysis.analysis[token].part + ':' +
                            analysis.analysis[token].morphology;
                 },
                 traversal)) {
            const auto &token = analysis.analysis[entry.token];
            signature.push_back(entry.incoming_label + '|' + token.part + ':' +
                                token.morphology);
        }
        return signature;
    };
    const auto canonical_event_signature = [](const auto &analysis) {
        std::vector<std::string> signature;
        for (const auto &event : parsers::markov::parser_canonical_events(
                 analysis, [&](const auto token) {
                     return analysis.analysis[token].part + ':' +
                            analysis.analysis[token].morphology;
                 })) {
            const auto &token = analysis.analysis[event.token];
            signature.push_back(
                event.kind == parsers::markov::CanonicalEventKind::enter
                    ? "enter|" + event.incoming_label + '|' + token.part + ':' +
                          token.morphology
                    : "exit|" + event.incoming_label);
        }
        return signature;
    };
    require(
        canonical_signature(svo, parsers::markov::TraversalOrder::head_first) ==
            canonical_signature(vos,
                                parsers::markov::TraversalOrder::head_first),
        "head-first parser order must preserve reordered trees");
    require(
        canonical_signature(svo, parsers::markov::TraversalOrder::head_last) ==
            canonical_signature(vos,
                                parsers::markov::TraversalOrder::head_last),
        "head-last parser order must preserve reordered trees");

    const parsers::RankedMorphologyAnalysis repeated_siblings{
        "repeated-siblings",
        0.0,
        {make_token(0U, "v", "verb", "V"), make_token(1U, "n", "noun", "N"),
         make_token(2U, "a", "adjective", "A"),
         make_token(3U, "n", "noun", "N"), make_token(4U, "b", "noun", "B")},
        {{0U, std::nullopt, "root"},
         {1U, 0U, "obj"},
         {2U, 1U, "amod"},
         {3U, 0U, "obj"},
         {4U, 3U, "nmod"}},
        false,
        false};
    const parsers::RankedMorphologyAnalysis permuted_repeated_siblings{
        "permuted-repeated-siblings",
        0.0,
        {make_token(0U, "v", "verb", "V"), make_token(1U, "n", "noun", "N"),
         make_token(2U, "b", "noun", "B"), make_token(3U, "n", "noun", "N"),
         make_token(4U, "a", "adjective", "A")},
        {{0U, std::nullopt, "root"},
         {1U, 0U, "obj"},
         {2U, 1U, "nmod"},
         {3U, 0U, "obj"},
         {4U, 3U, "amod"}},
        false,
        false};
    for (const auto traversal : {parsers::markov::TraversalOrder::head_first,
                                 parsers::markov::TraversalOrder::head_last}) {
        require(canonical_signature(repeated_siblings, traversal) ==
                    canonical_signature(permuted_repeated_siblings, traversal),
                "recursive sibling order must ignore token permutation");
    }
    require(canonical_event_signature(repeated_siblings) ==
                canonical_event_signature(permuted_repeated_siblings),
            "structural events must ignore token permutation");

    const auto topology_tokens = std::vector{make_token(0U, "v", "verb", "V"),
                                             make_token(1U, "n", "noun", "N"),
                                             make_token(2U, "p", "part", "P")};
    const parsers::RankedMorphologyAnalysis sibling_topology{
        "sibling-topology",
        0.0,
        topology_tokens,
        {{0U, std::nullopt, "root"}, {1U, 0U, "dep"}, {2U, 0U, "dep"}},
        false,
        false};
    const parsers::RankedMorphologyAnalysis nested_topology{
        "nested-topology",
        0.0,
        topology_tokens,
        {{0U, std::nullopt, "root"}, {1U, 0U, "dep"}, {2U, 1U, "dep"}},
        false,
        false};
    require(
        canonical_signature(sibling_topology,
                            parsers::markov::TraversalOrder::head_first) ==
            canonical_signature(nested_topology,
                                parsers::markov::TraversalOrder::head_first),
        "token-only baseline should expose its topology collision");
    require(canonical_event_signature(sibling_topology) !=
                canonical_event_signature(nested_topology),
            "structural events must retain subtree boundaries");

    const parsers::RankedMorphologyAnalysis intended_attachment{
        "intended-attachment",
        0.0,
        {make_token(0U, "sum", "verb", "finite"),
         make_token(1U, "mens", "noun", "nominative"),
         make_token(2U, "sanus", "adjective", "nominative"),
         make_token(3U, "in", "preposition", "ablative"),
         make_token(4U, "corpus", "noun", "ablative"),
         make_token(5U, "sanus", "adjective", "ablative")},
        {{5U, 4U, "amod"},
         {3U, 4U, "case"},
         {0U, std::nullopt, "root"},
         {4U, 0U, "obl"},
         {2U, 0U, "predicative"},
         {1U, 0U, "nsubj"}},
        true,
        true};
    auto permuted_relations = intended_attachment;
    std::ranges::reverse(permuted_relations.relations);
    const auto dependency_state = [&](const std::size_t token) {
        const auto &choice = intended_attachment.analysis[token];
        return choice.part + ':' + choice.morphology;
    };
    const auto intended_order_two = parsers::dependency_markov::extract_factors(
        intended_attachment, dependency_state, 2U);
    const auto permuted_order_two = parsers::dependency_markov::extract_factors(
        permuted_relations, dependency_state, 2U);
    require(intended_order_two == permuted_order_two,
            "dependency factors must ignore relation visit order");
    require(intended_order_two.size() == intended_attachment.analysis.size(),
            "root and every dependent must contribute exactly once");
    require(std::ranges::count_if(
                intended_order_two,
                [](const auto &factor) {
                    return factor.kind ==
                           parsers::dependency_markov::FactorKind::root;
                }) == 1,
            "dependency factors must contain one root factor");
    require(std::ranges::all_of(intended_order_two | std::views::drop(1),
                                [](const auto &factor) {
                                    return factor.contexts.size() == 2U;
                                }),
            "order two must carry local head and grandparent contexts");

    auto direct_predicative = intended_attachment;
    direct_predicative.assignment_id = "direct-predicative";
    for (auto &relation : direct_predicative.relations) {
        if (relation.dependent == 5U) {
            relation.head = 0U;
            relation.label = "predicative";
        }
    }
    const auto direct_order_two = parsers::dependency_markov::extract_factors(
        direct_predicative, dependency_state, 2U);
    require(intended_order_two != direct_order_two,
            "head and label changes must reach dependency factors");

    parsers::dependency_markov::Model dependency_model{2U, 0.1, 1.0};
    dependency_model.train(intended_order_two);
    dependency_model.train(intended_order_two);
    require(dependency_model.score(intended_order_two).log_score >
                dependency_model.score(direct_order_two).log_score,
            "dependency model must prefer its observed local attachment");

    const auto intended_with_profile =
        parsers::dependency_markov::extract_factors(intended_attachment,
                                                    dependency_state, 2U, true);
    const auto direct_with_profile =
        parsers::dependency_markov::extract_factors(direct_predicative,
                                                    dependency_state, 2U, true);
    require(intended_with_profile.size() == intended_order_two.size() + 1U,
            "one verbal predicate profile expected");
    require(intended_with_profile.back().kind ==
                    parsers::dependency_markov::FactorKind::predicate_profile &&
                intended_with_profile.back().event !=
                    direct_with_profile.back().event,
            "predicate profile must jointly expose changed dependents");
    parsers::dependency_markov::Model profile_model{2U, 0.1, 1.0, true};
    profile_model.train(intended_with_profile);
    profile_model.train(intended_with_profile);
    require(profile_model.score(intended_with_profile).log_score >
                profile_model.score(direct_with_profile).log_score,
            "joint predicate profile must affect the candidate score");

    rejected = false;
    try {
        auto malformed = intended_attachment;
        malformed.relations.push_back({0U, 1U, "duplicate"});
        static_cast<void>(parsers::dependency_markov::extract_factors(
            malformed, dependency_state, 1U));
    } catch (const std::invalid_argument &) {
        rejected = true;
    }
    require(rejected, "malformed dependency candidate rejection");

    std::cout << "markov-model-test: passed\n";
}
