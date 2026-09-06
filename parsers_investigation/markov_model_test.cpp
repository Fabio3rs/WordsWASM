#include "markov_features.hpp"
#include "markov_model.hpp"

#include <cmath>
#include <iostream>
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
    require(std::isfinite(model.log_probability({"UNSEEN", "VERB"})),
            "smoothing for unseen state");

    parsers::MarkovModel weighted{2U};
    weighted.train({"noun", "verb"}, 0.5);
    weighted.train({"noun", "adjective"}, 0.5);
    require(near(weighted.training_weight(), 1.0), "fractional weights");

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
        parsers::MarkovModel empty{1U};
        static_cast<void>(empty.log_probability({"noun"}));
    } catch (const std::logic_error &) {
        rejected = true;
    }
    require(rejected, "untrained model rejection");

    const auto make_token = [](const std::size_t token,
                               const std::string &lemma,
                               const std::string &part,
                               const std::string &morphology) {
        return parsers::AnalysisChoice{token, token, lemma, part, morphology};
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

    std::cout << "markov-model-test: passed\n";
}
