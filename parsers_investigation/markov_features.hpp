#pragma once

#include "parser.hpp"

#include <algorithm>
#include <cstddef>
#include <numeric>
#include <string>
#include <string_view>
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

enum class CanonicalEventKind { enter, exit };

struct CanonicalEvent final {
    std::size_t token{};
    std::string incoming_label;
    CanonicalEventKind kind{CanonicalEventKind::enter};
    bool operator==(const CanonicalEvent &) const = default;
};

namespace detail {

struct CanonicalLayout final {
    std::vector<std::string> incoming_labels;
    std::vector<std::vector<std::size_t>> children;
    std::vector<std::size_t> starts;
};

inline void append_component(std::string &target,
                             const std::string_view component) {
    target += std::to_string(component.size());
    target += ':';
    target += component;
}

// The recursive signature makes sibling order independent of token indices.
// Length-prefixed fields and child signatures avoid delimiter ambiguities.
template <typename StateProjection>
[[nodiscard]] CanonicalLayout
canonical_layout(const RankedMorphologyAnalysis &analysis,
                 StateProjection &&project_state) {
    const auto token_count = analysis.analysis.size();
    CanonicalLayout layout{std::vector<std::string>(token_count, "unattached"),
                           std::vector<std::vector<std::size_t>>(token_count),
                           {}};
    std::vector<std::size_t> roots;
    for (const auto &relation : analysis.relations) {
        if (relation.dependent >= token_count) {
            continue;
        }
        layout.incoming_labels[relation.dependent] = relation.label;
        if (relation.head && *relation.head < token_count) {
            layout.children[*relation.head].push_back(relation.dependent);
        } else {
            roots.push_back(relation.dependent);
        }
    }

    std::vector<std::string> projected_states;
    projected_states.reserve(token_count);
    for (std::size_t token = 0; token < token_count; ++token) {
        projected_states.emplace_back(project_state(token));
    }

    std::vector<std::string> signatures(token_count);
    std::vector<unsigned char> signature_state(token_count);
    const auto build_signature =
        [&](this auto &&self, const std::size_t token) -> const std::string & {
        if (signature_state[token] == 2U) {
            return signatures[token];
        }
        if (signature_state[token] == 1U) {
            // Parser candidates are trees. This marker merely keeps diagnostic
            // use deterministic if malformed cyclic input reaches the helper.
            signatures[token] = "cycle";
            append_component(signatures[token], layout.incoming_labels[token]);
            append_component(signatures[token], projected_states[token]);
            append_component(signatures[token], analysis.analysis[token].lemma);
            return signatures[token];
        }
        signature_state[token] = 1U;
        for (const auto child : layout.children[token]) {
            static_cast<void>(self(child));
        }
        std::ranges::stable_sort(
            layout.children[token], [&](const auto left, const auto right) {
                if (signatures[left] != signatures[right]) {
                    return signatures[left] < signatures[right];
                }
                // Equal recursive signatures emit equal represented
                // subtrees, so this fallback cannot change the serialization.
                return left < right;
            });

        auto &signature = signatures[token];
        signature = "node";
        append_component(signature, layout.incoming_labels[token]);
        append_component(signature, projected_states[token]);
        append_component(signature, analysis.analysis[token].lemma);
        for (const auto child : layout.children[token]) {
            append_component(signature, signatures[child]);
        }
        signature += "end";
        signature_state[token] = 2U;
        return signature;
    };
    for (std::size_t token = 0; token < token_count; ++token) {
        static_cast<void>(build_signature(token));
    }

    const auto order_nodes = [&](auto &nodes) {
        std::ranges::stable_sort(nodes, [&](const auto left, const auto right) {
            if (signatures[left] != signatures[right]) {
                return signatures[left] < signatures[right];
            }
            return left < right;
        });
    };
    order_nodes(roots);
    layout.starts = std::move(roots);

    std::vector<std::size_t> remaining(token_count);
    std::iota(remaining.begin(), remaining.end(), 0U);
    order_nodes(remaining);
    layout.starts.insert(layout.starts.end(), remaining.begin(),
                         remaining.end());
    return layout;
}

} // namespace detail

// Produces a traversal whose represented order is independent of surface
// position. Siblings are ordered by recursive subtree signatures; token
// position is consulted only for structurally indistinguishable signatures.
template <typename StateProjection>
[[nodiscard]] std::vector<CanonicalToken> parser_canonical_order(
    const RankedMorphologyAnalysis &analysis, StateProjection &&project_state,
    const TraversalOrder traversal = TraversalOrder::head_first) {
    const auto token_count = analysis.analysis.size();
    auto layout = detail::canonical_layout(
        analysis, std::forward<StateProjection>(project_state));

    std::vector<bool> visited(token_count);
    std::vector<CanonicalToken> result;
    result.reserve(token_count);
    const auto visit = [&](this auto &&self, const std::size_t token) -> void {
        if (token >= token_count || visited[token]) {
            return;
        }
        visited[token] = true;
        if (traversal == TraversalOrder::head_first) {
            result.push_back(
                CanonicalToken{token, layout.incoming_labels[token]});
        }
        for (const auto child : layout.children[token]) {
            self(child);
        }
        if (traversal == TraversalOrder::head_last) {
            result.push_back(
                CanonicalToken{token, layout.incoming_labels[token]});
        }
    };
    for (const auto token : layout.starts) {
        visit(token);
    }
    return result;
}

// Unlike the token-only traversal, enter/exit events retain subtree
// boundaries and therefore distinguish different represented topologies.
template <typename StateProjection>
[[nodiscard]] std::vector<CanonicalEvent>
parser_canonical_events(const RankedMorphologyAnalysis &analysis,
                        StateProjection &&project_state) {
    const auto token_count = analysis.analysis.size();
    auto layout = detail::canonical_layout(
        analysis, std::forward<StateProjection>(project_state));
    std::vector<bool> visited(token_count);
    std::vector<CanonicalEvent> result;
    result.reserve(token_count * 2U);
    const auto visit = [&](this auto &&self, const std::size_t token) -> void {
        if (token >= token_count || visited[token]) {
            return;
        }
        visited[token] = true;
        result.push_back(CanonicalEvent{token, layout.incoming_labels[token],
                                        CanonicalEventKind::enter});
        for (const auto child : layout.children[token]) {
            self(child);
        }
        result.push_back(CanonicalEvent{token, layout.incoming_labels[token],
                                        CanonicalEventKind::exit});
    };
    for (const auto token : layout.starts) {
        visit(token);
    }
    return result;
}

} // namespace parsers::markov
