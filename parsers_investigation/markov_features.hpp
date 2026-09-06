#pragma once

#include "parser.hpp"

#include <algorithm>
#include <cstddef>
#include <numeric>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace parsers::markov {

enum class TraversalOrder { head_first, head_last };

struct CanonicalToken final {
    std::size_t token{};
    std::string incoming_label;
    bool operator==(const CanonicalToken &) const = default;
};

// Produces a root-first traversal independent of surface position whenever
// relation, projected state and lemma distinguish siblings. Token position is
// only the final tie-break; tied nodes then emit the same projected state.
template <typename StateProjection>
[[nodiscard]] std::vector<CanonicalToken> parser_canonical_order(
    const RankedMorphologyAnalysis &analysis, StateProjection &&project_state,
    const TraversalOrder traversal = TraversalOrder::head_first) {
    const auto token_count = analysis.analysis.size();
    std::vector<std::string> incoming_labels(token_count, "unattached");
    std::vector<std::vector<std::size_t>> children(token_count);
    std::vector<std::size_t> roots;
    for (const auto &relation : analysis.relations) {
        if (relation.dependent >= token_count) {
            continue;
        }
        incoming_labels[relation.dependent] = relation.label;
        if (relation.head && *relation.head < token_count) {
            children[*relation.head].push_back(relation.dependent);
        } else {
            roots.push_back(relation.dependent);
        }
    }

    const auto key = [&](const std::size_t token) {
        return std::tuple{incoming_labels[token], project_state(token),
                          analysis.analysis[token].lemma, token};
    };
    const auto order_nodes = [&](auto &nodes) {
        std::ranges::stable_sort(nodes, [&](const auto left, const auto right) {
            return key(left) < key(right);
        });
    };
    order_nodes(roots);
    for (auto &siblings : children) {
        order_nodes(siblings);
    }

    std::vector<bool> visited(token_count);
    std::vector<CanonicalToken> result;
    result.reserve(token_count);
    const auto visit = [&](this auto &&self, const std::size_t token) -> void {
        if (token >= token_count || visited[token]) {
            return;
        }
        visited[token] = true;
        if (traversal == TraversalOrder::head_first) {
            result.push_back(CanonicalToken{token, incoming_labels[token]});
        }
        for (const auto child : children[token]) {
            self(child);
        }
        if (traversal == TraversalOrder::head_last) {
            result.push_back(CanonicalToken{token, incoming_labels[token]});
        }
    };
    for (const auto root : roots) {
        visit(root);
    }

    std::vector<std::size_t> unattached(token_count);
    std::iota(unattached.begin(), unattached.end(), 0U);
    order_nodes(unattached);
    for (const auto token : unattached) {
        visit(token);
    }
    return result;
}

} // namespace parsers::markov
