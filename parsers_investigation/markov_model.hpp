#pragma once

#include <cmath>
#include <cstddef>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace parsers {

enum class MarkovSmoothing { additive, hierarchical_backoff };

struct MarkovScoreDiagnostics final {
    std::size_t transitions{};
    std::size_t unknown_states{};
    std::size_t full_context_hits{};
    std::size_t full_context_misses{};
    std::size_t uniform_fallbacks{};
    std::size_t backed_off_transitions{};
    std::size_t no_observed_context{};
    std::vector<std::size_t> deepest_observed_context_hits;

    void merge(const MarkovScoreDiagnostics &other) {
        transitions += other.transitions;
        unknown_states += other.unknown_states;
        full_context_hits += other.full_context_hits;
        full_context_misses += other.full_context_misses;
        uniform_fallbacks += other.uniform_fallbacks;
        backed_off_transitions += other.backed_off_transitions;
        no_observed_context += other.no_observed_context;
        if (deepest_observed_context_hits.size() <
            other.deepest_observed_context_hits.size()) {
            deepest_observed_context_hits.resize(
                other.deepest_observed_context_hits.size());
        }
        for (std::size_t index = 0;
             index < other.deepest_observed_context_hits.size(); ++index) {
            deepest_observed_context_hits[index] +=
                other.deepest_observed_context_hits[index];
        }
    }
};

struct MarkovScoreResult final {
    double log_probability{};
    MarkovScoreDiagnostics diagnostics;
};

// Deliberately small count-based baseline. Both selectable estimators ensure
// that an unseen transition penalizes a possible parse instead of turning it
// into an impossible parse.
class MarkovModel final {
  public:
    using Context = std::vector<std::string>;

    explicit MarkovModel(
        const std::size_t order, const double alpha = 0.1,
        const MarkovSmoothing smoothing = MarkovSmoothing::additive,
        const double backoff_strength = 1.0)
        : order_{order}, alpha_{alpha}, smoothing_{smoothing},
          backoff_strength_{backoff_strength} {
        if (order_ == 0U) {
            throw std::invalid_argument{"Markov order must be positive"};
        }
        if (!(alpha_ > 0.0) || !std::isfinite(alpha_)) {
            throw std::invalid_argument{
                "Markov smoothing alpha must be finite and positive"};
        }
        if (!(backoff_strength_ > 0.0) || !std::isfinite(backoff_strength_)) {
            throw std::invalid_argument{
                "Markov backoff strength must be finite and positive"};
        }
        vocabulary_.insert(std::string{unknown_token});
        vocabulary_.insert(std::string{end_token});
        if (smoothing_ == MarkovSmoothing::hierarchical_backoff) {
            backoff_transitions_.resize(order_ + 1U);
        }
    }

    void train(const std::vector<std::string> &sequence,
               const double weight = 1.0) {
        if (!(weight > 0.0) || !std::isfinite(weight)) {
            throw std::invalid_argument{
                "Markov training weight must be finite and positive"};
        }
        for (const auto &state : sequence) {
            validate_state(state);
            vocabulary_.insert(state);
        }

        Context context(order_, std::string{start_token});
        for (const auto &state : sequence) {
            observe_transition(context, state, weight);
            advance(context, state);
        }
        observe_transition(context, std::string{end_token}, weight);
        training_weight_ += weight;
    }

    [[nodiscard]] MarkovScoreResult
    score(const std::vector<std::string> &sequence) const {
        if (training_weight_ == 0.0) {
            throw std::logic_error{"Markov model has no training sequences"};
        }
        MarkovScoreResult result;
        result.diagnostics.deepest_observed_context_hits.resize(order_ + 1U);
        Context context(order_, std::string{start_token});
        for (const auto &raw_state : sequence) {
            validate_state(raw_state);
            if (!vocabulary_.contains(raw_state)) {
                ++result.diagnostics.unknown_states;
            }
            const auto state = normalized(raw_state);
            result.log_probability +=
                std::log(probability(context, state, result.diagnostics));
            advance(context, state);
        }
        result.log_probability += std::log(
            probability(context, std::string{end_token}, result.diagnostics));
        return result;
    }

    [[nodiscard]] double
    log_probability(const std::vector<std::string> &sequence) const {
        return score(sequence).log_probability;
    }

    [[nodiscard]] std::size_t order() const noexcept { return order_; }
    [[nodiscard]] double alpha() const noexcept { return alpha_; }
    [[nodiscard]] MarkovSmoothing smoothing() const noexcept {
        return smoothing_;
    }
    [[nodiscard]] double backoff_strength() const noexcept {
        return backoff_strength_;
    }
    [[nodiscard]] double training_weight() const noexcept {
        return training_weight_;
    }
    [[nodiscard]] std::size_t vocabulary_size() const noexcept {
        return vocabulary_.size();
    }

  private:
    struct Row final {
        std::map<std::string, double, std::less<>> next;
        double total{};
    };

    static constexpr std::string_view start_token{"<s>"};
    static constexpr std::string_view end_token{"</s>"};
    static constexpr std::string_view unknown_token{"<unk>"};

    std::size_t order_{};
    double alpha_{};
    MarkovSmoothing smoothing_{MarkovSmoothing::additive};
    double backoff_strength_{};
    std::map<Context, Row> transitions_;
    std::vector<std::map<Context, Row>> backoff_transitions_;
    std::set<std::string, std::less<>> vocabulary_;
    double training_weight_{};

    static void validate_state(const std::string_view state) {
        if (state.empty() || state == start_token || state == end_token ||
            state == unknown_token) {
            throw std::invalid_argument{"invalid reserved Markov state"};
        }
    }

    static void observe(std::map<Context, Row> &table, const Context &context,
                        const std::string &next, const double weight) {
        auto &row = table[context];
        row.next[next] += weight;
        row.total += weight;
    }

    [[nodiscard]] static Context suffix(const Context &context,
                                        const std::size_t depth) {
        return Context(context.end() - static_cast<std::ptrdiff_t>(depth),
                       context.end());
    }

    void observe_transition(const Context &context, const std::string &next,
                            const double weight) {
        if (smoothing_ == MarkovSmoothing::additive) {
            observe(transitions_, context, next, weight);
            return;
        }
        for (std::size_t depth = 0; depth <= order_; ++depth) {
            observe(backoff_transitions_[depth], suffix(context, depth), next,
                    weight);
        }
    }

    [[nodiscard]] std::string normalized(const std::string &state) const {
        return vocabulary_.contains(state) ? state : std::string{unknown_token};
    }

    [[nodiscard]] Context normalized_context(const Context &raw_context) const {
        Context context;
        context.reserve(raw_context.size());
        for (const auto &state : raw_context) {
            context.push_back(state == start_token ? state : normalized(state));
        }
        return context;
    }

    [[nodiscard]] double additive_probability(const Context &context,
                                              const std::string &next) const {
        const auto row = transitions_.find(context);
        const double total =
            row == transitions_.end() ? 0.0 : row->second.total;
        double count{};
        if (row != transitions_.end()) {
            const auto found = row->second.next.find(next);
            if (found != row->second.next.end()) {
                count = found->second;
            }
        }
        const auto outcomes = static_cast<double>(vocabulary_.size());
        return (count + alpha_) / (total + alpha_ * outcomes);
    }

    [[nodiscard]] double
    hierarchical_probability(const Context &context,
                             const std::string &next) const {
        const auto outcomes = static_cast<double>(vocabulary_.size());
        const Context empty;
        const auto base = backoff_transitions_.front().find(empty);
        const auto base_total = base == backoff_transitions_.front().end()
                                    ? 0.0
                                    : base->second.total;
        double base_count{};
        if (base != backoff_transitions_.front().end()) {
            const auto found = base->second.next.find(next);
            if (found != base->second.next.end()) {
                base_count = found->second;
            }
        }
        double result =
            (base_count + alpha_) / (base_total + alpha_ * outcomes);
        for (std::size_t depth = 1U; depth <= order_; ++depth) {
            const auto reduced = suffix(context, depth);
            const auto row = backoff_transitions_[depth].find(reduced);
            if (row == backoff_transitions_[depth].end()) {
                continue;
            }
            double count{};
            const auto found = row->second.next.find(next);
            if (found != row->second.next.end()) {
                count = found->second;
            }
            result = (count + backoff_strength_ * result) /
                     (row->second.total + backoff_strength_);
        }
        return result;
    }

    [[nodiscard]] double
    probability(const Context &raw_context, const std::string &next,
                MarkovScoreDiagnostics &diagnostics) const {
        ++diagnostics.transitions;
        const auto context = normalized_context(raw_context);
        if (smoothing_ == MarkovSmoothing::additive) {
            const auto row = transitions_.find(context);
            if (row == transitions_.end()) {
                ++diagnostics.full_context_misses;
                ++diagnostics.uniform_fallbacks;
                ++diagnostics.no_observed_context;
            } else {
                ++diagnostics.full_context_hits;
                ++diagnostics.deepest_observed_context_hits[order_];
            }
            return additive_probability(context, next);
        }

        std::optional<std::size_t> deepest;
        for (std::size_t depth = order_ + 1U; depth-- > 0U;) {
            const auto reduced = suffix(context, depth);
            if (backoff_transitions_[depth].contains(reduced)) {
                deepest = depth;
                break;
            }
        }
        const auto full_context = suffix(context, order_);
        if (backoff_transitions_[order_].contains(full_context)) {
            ++diagnostics.full_context_hits;
        } else {
            ++diagnostics.full_context_misses;
            ++diagnostics.backed_off_transitions;
        }
        if (deepest) {
            ++diagnostics.deepest_observed_context_hits[*deepest];
        } else {
            ++diagnostics.no_observed_context;
        }
        return hierarchical_probability(context, next);
    }

    static void advance(Context &context, const std::string &state) {
        context.erase(context.begin());
        context.push_back(state);
    }
};

} // namespace parsers
