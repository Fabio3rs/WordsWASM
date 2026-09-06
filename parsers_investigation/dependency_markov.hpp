#pragma once

#include "parser.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <map>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace parsers::dependency_markov {

enum class FactorKind { root, arc, predicate_profile };

// Contexts are ordered from least to most specific.  Order 1 contains the
// local head; order 2 additionally contains grandparent, incoming head label,
// and local head.  The event is the selected root state or a labeled
// dependent state.
struct Factor final {
    FactorKind kind{FactorKind::arc};
    std::size_t dependent{};
    std::vector<std::string> contexts;
    std::string event;
    bool operator==(const Factor &) const = default;
};

struct Diagnostics final {
    std::size_t factors{};
    std::size_t unknown_events{};
    std::size_t full_context_hits{};
    std::size_t full_context_misses{};
    std::size_t backed_off_factors{};
    std::vector<std::size_t> deepest_context_hits;

    void merge(const Diagnostics &other) {
        factors += other.factors;
        unknown_events += other.unknown_events;
        full_context_hits += other.full_context_hits;
        full_context_misses += other.full_context_misses;
        backed_off_factors += other.backed_off_factors;
        if (deepest_context_hits.size() < other.deepest_context_hits.size()) {
            deepest_context_hits.resize(other.deepest_context_hits.size());
        }
        for (std::size_t index = 0; index < other.deepest_context_hits.size();
             ++index) {
            deepest_context_hits[index] += other.deepest_context_hits[index];
        }
    }
};

struct Score final {
    double log_score{};
    Diagnostics diagnostics;
};

namespace detail {

inline void append_component(std::string &target,
                             const std::string_view value) {
    target += std::to_string(value.size());
    target += ':';
    target += value;
}

inline std::string component_pair(const std::string_view name,
                                  const std::string_view value) {
    std::string result;
    append_component(result, name);
    append_component(result, value);
    return result;
}

inline std::string arc_event(const std::string_view label,
                             const std::string_view dependent_state) {
    std::string result;
    append_component(result, label);
    append_component(result, dependent_state);
    return result;
}

inline std::string ancestor_context(const std::string_view grandparent_state,
                                    const std::string_view head_label,
                                    const std::string_view head_state) {
    std::string result;
    append_component(result, grandparent_state);
    append_component(result, head_label);
    append_component(result, head_state);
    return result;
}

inline std::string profile_event(std::vector<std::string> dependents) {
    std::ranges::sort(dependents);
    std::string result;
    for (const auto &dependent : dependents) {
        append_component(result, dependent);
    }
    // An empty profile is a real observation: the predicate has no direct
    // dependents in this candidate.
    return result.empty() ? std::string{"0:"} : result;
}

} // namespace detail

// The extractor indexes direct heads rather than walking a DFS/BFS sequence.
// Consequently every token contributes exactly once and relation input order
// cannot change the factors or duplicate arcs near the root.
template <typename Analysis, typename StateProjection>
[[nodiscard]] std::vector<Factor>
extract_factors(const Analysis &analysis, StateProjection &&project_state,
                const std::size_t order,
                const bool include_predicate_profiles = false) {
    if (order == 0U || order > 2U) {
        throw std::invalid_argument{
            "dependency Markov order must be one or two"};
    }
    const auto token_count = analysis.analysis.size();
    if (token_count == 0U || analysis.relations.size() != token_count) {
        throw std::invalid_argument{
            "dependency candidate must have one relation per token"};
    }

    std::vector<std::optional<std::size_t>> heads(token_count);
    std::vector<std::string> labels(token_count);
    std::vector<bool> seen(token_count);
    std::optional<std::size_t> root;
    for (const auto &relation : analysis.relations) {
        if (relation.dependent >= token_count || seen[relation.dependent]) {
            throw std::invalid_argument{
                "dependency candidate has invalid or duplicate dependent"};
        }
        seen[relation.dependent] = true;
        labels[relation.dependent] = relation.label;
        heads[relation.dependent] = relation.head;
        if (relation.head) {
            if (*relation.head >= token_count ||
                *relation.head == relation.dependent) {
                throw std::invalid_argument{
                    "dependency candidate has invalid head"};
            }
        } else if (root) {
            throw std::invalid_argument{
                "dependency candidate must have exactly one root"};
        } else {
            root = relation.dependent;
        }
    }
    if (!root || std::ranges::find(seen, false) != seen.end()) {
        throw std::invalid_argument{
            "dependency candidate must have exactly one root"};
    }

    std::vector<std::string> states;
    states.reserve(token_count);
    for (std::size_t token = 0; token < token_count; ++token) {
        states.emplace_back(project_state(token));
        if (states.back().empty()) {
            throw std::invalid_argument{
                "dependency state projection must not be empty"};
        }
        std::vector<bool> path(token_count);
        auto cursor = token;
        while (heads[cursor]) {
            if (path[cursor]) {
                throw std::invalid_argument{
                    "dependency candidate must be acyclic"};
            }
            path[cursor] = true;
            cursor = *heads[cursor];
        }
        if (cursor != *root) {
            throw std::invalid_argument{
                "dependency candidate must be connected to its root"};
        }
    }

    std::vector<Factor> factors;
    factors.reserve(token_count);
    factors.push_back(Factor{FactorKind::root, *root, {}, states[*root]});
    for (std::size_t dependent = 0; dependent < token_count; ++dependent) {
        if (dependent == *root) {
            continue;
        }
        const auto head = *heads[dependent];
        Factor factor{FactorKind::arc,
                      dependent,
                      {detail::component_pair("head", states[head])},
                      detail::arc_event(labels[dependent], states[dependent])};
        if (order == 2U) {
            const auto grandparent_state =
                heads[head] ? states[*heads[head]] : std::string{"<ROOT>"};
            const auto head_label =
                heads[head] ? labels[head] : std::string{"root"};
            factor.contexts.push_back(detail::ancestor_context(
                grandparent_state, head_label, states[head]));
        }
        factors.push_back(std::move(factor));
    }
    if (include_predicate_profiles) {
        for (std::size_t predicate = 0; predicate < token_count; ++predicate) {
            if (analysis.analysis[predicate].part != "verb") {
                continue;
            }
            std::vector<std::string> dependents;
            for (std::size_t dependent = 0; dependent < token_count;
                 ++dependent) {
                if (heads[dependent] == predicate) {
                    dependents.push_back(detail::arc_event(labels[dependent],
                                                           states[dependent]));
                }
            }
            factors.push_back(Factor{
                FactorKind::predicate_profile,
                predicate,
                {detail::component_pair("predicate", states[predicate])},
                detail::profile_event(std::move(dependents)),
            });
        }
    }
    return factors;
}

class ConditionalCounts final {
  public:
    explicit ConditionalCounts(const std::size_t levels,
                               const double alpha = 0.1,
                               const double backoff_strength = 1.0)
        : tables_(levels + 1U), alpha_{alpha},
          backoff_strength_{backoff_strength} {
        if (!(alpha_ > 0.0) || !std::isfinite(alpha_)) {
            throw std::invalid_argument{
                "dependency smoothing alpha must be finite and positive"};
        }
        if (!(backoff_strength_ > 0.0) || !std::isfinite(backoff_strength_)) {
            throw std::invalid_argument{
                "dependency backoff strength must be finite and positive"};
        }
        vocabulary_.insert(std::string{unknown_event});
    }

    void observe(const std::vector<std::string> &contexts,
                 const std::string &event, const double weight) {
        if (contexts.size() + 1U != tables_.size() || event.empty()) {
            throw std::invalid_argument{
                "dependency factor has incompatible context depth"};
        }
        if (!(weight > 0.0) || !std::isfinite(weight)) {
            throw std::invalid_argument{
                "dependency training weight must be finite and positive"};
        }
        vocabulary_.insert(event);
        add(tables_[0U], std::string{}, event, weight);
        for (std::size_t level = 0; level < contexts.size(); ++level) {
            add(tables_[level + 1U], contexts[level], event, weight);
        }
        training_weight_ += weight;
    }

    [[nodiscard]] double probability(const std::vector<std::string> &contexts,
                                     const std::string &raw_event,
                                     Diagnostics &diagnostics) const {
        if (training_weight_ == 0.0) {
            throw std::logic_error{
                "dependency conditional model has no training factors"};
        }
        if (contexts.size() + 1U != tables_.size()) {
            throw std::invalid_argument{
                "dependency factor has incompatible context depth"};
        }
        ++diagnostics.factors;
        const auto known = vocabulary_.contains(raw_event);
        if (!known) {
            ++diagnostics.unknown_events;
        }
        const auto event = known ? raw_event : std::string{unknown_event};
        const auto outcomes = static_cast<double>(vocabulary_.size());
        const auto &base = tables_[0U].at(std::string{});
        double result =
            (count(base, event) + alpha_) / (base.total + alpha_ * outcomes);
        std::size_t deepest{};
        for (std::size_t level = 0; level < contexts.size(); ++level) {
            const auto found = tables_[level + 1U].find(contexts[level]);
            if (found == tables_[level + 1U].end()) {
                continue;
            }
            deepest = level + 1U;
            result =
                (count(found->second, event) + backoff_strength_ * result) /
                (found->second.total + backoff_strength_);
        }
        if (diagnostics.deepest_context_hits.size() < tables_.size()) {
            diagnostics.deepest_context_hits.resize(tables_.size());
        }
        ++diagnostics.deepest_context_hits[deepest];
        if (contexts.empty() || deepest == contexts.size()) {
            ++diagnostics.full_context_hits;
        } else {
            ++diagnostics.full_context_misses;
            ++diagnostics.backed_off_factors;
        }
        return result;
    }

    [[nodiscard]] double training_weight() const noexcept {
        return training_weight_;
    }

  private:
    struct Row final {
        std::map<std::string, double, std::less<>> events;
        double total{};
    };

    static constexpr std::string_view unknown_event{"<UNK>"};
    std::vector<std::map<std::string, Row, std::less<>>> tables_;
    std::set<std::string, std::less<>> vocabulary_;
    double alpha_{};
    double backoff_strength_{};
    double training_weight_{};

    static void add(std::map<std::string, Row, std::less<>> &table,
                    const std::string &context, const std::string &event,
                    const double weight) {
        auto &row = table[context];
        row.events[event] += weight;
        row.total += weight;
    }

    [[nodiscard]] static double count(const Row &row,
                                      const std::string &event) {
        const auto found = row.events.find(event);
        return found == row.events.end() ? 0.0 : found->second;
    }
};

class Model final {
  public:
    explicit Model(const std::size_t order, const double alpha = 0.1,
                   const double backoff_strength = 1.0,
                   const bool include_predicate_profiles = false)
        : order_{order}, roots_{0U, alpha, backoff_strength},
          arcs_{order, alpha, backoff_strength},
          profiles_{1U, alpha, backoff_strength},
          include_predicate_profiles_{include_predicate_profiles} {
        if (order_ == 0U || order_ > 2U) {
            throw std::invalid_argument{
                "dependency Markov order must be one or two"};
        }
    }

    void train(const std::vector<Factor> &factors, const double weight = 1.0) {
        validate_factor_set(factors);
        for (const auto &factor : factors) {
            if (factor.kind == FactorKind::root) {
                roots_.observe(factor.contexts, factor.event, weight);
            } else if (factor.kind == FactorKind::arc) {
                arcs_.observe(factor.contexts, factor.event, weight);
            } else {
                profiles_.observe(factor.contexts, factor.event, weight);
            }
        }
        training_tree_weight_ += weight;
    }

    [[nodiscard]] Score score(const std::vector<Factor> &factors) const {
        if (training_tree_weight_ == 0.0) {
            throw std::logic_error{"dependency Markov model has no trees"};
        }
        validate_factor_set(factors);
        Score result;
        for (const auto &factor : factors) {
            double probability{};
            if (factor.kind == FactorKind::root) {
                probability = roots_.probability(factor.contexts, factor.event,
                                                 result.diagnostics);
            } else if (factor.kind == FactorKind::arc) {
                probability = arcs_.probability(factor.contexts, factor.event,
                                                result.diagnostics);
            } else {
                probability = profiles_.probability(
                    factor.contexts, factor.event, result.diagnostics);
            }
            result.log_score += std::log(probability);
        }
        return result;
    }

    [[nodiscard]] std::size_t order() const noexcept { return order_; }
    [[nodiscard]] double training_tree_weight() const noexcept {
        return training_tree_weight_;
    }

  private:
    std::size_t order_{};
    ConditionalCounts roots_;
    ConditionalCounts arcs_;
    ConditionalCounts profiles_;
    bool include_predicate_profiles_{};
    double training_tree_weight_{};

    void validate_factor_set(const std::vector<Factor> &factors) const {
        if (factors.empty() || factors.front().kind != FactorKind::root ||
            factors.front().contexts.size() != 0U ||
            std::ranges::count_if(factors, [](const Factor &factor) {
                return factor.kind == FactorKind::root;
            }) != 1) {
            throw std::invalid_argument{
                "dependency factor set must contain exactly one root"};
        }
        for (const auto &factor : factors) {
            if (factor.event.empty() ||
                (factor.kind == FactorKind::arc &&
                 factor.contexts.size() != order_) ||
                (factor.kind == FactorKind::predicate_profile &&
                 factor.contexts.size() != 1U)) {
                throw std::invalid_argument{
                    "dependency factor set has incompatible order"};
            }
        }
        const auto profile_count =
            std::ranges::count_if(factors, [](const Factor &factor) {
                return factor.kind == FactorKind::predicate_profile;
            });
        if ((!include_predicate_profiles_ && profile_count != 0) ||
            (include_predicate_profiles_ && profile_count == 0)) {
            throw std::invalid_argument{
                "dependency predicate-profile configuration mismatch"};
        }
    }
};

} // namespace parsers::dependency_markov
