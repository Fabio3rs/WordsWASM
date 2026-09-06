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

// Deliberately small count-based baseline. Additive smoothing ensures that an
// unseen transition penalizes a possible parse instead of turning it into an
// impossible parse.
class MarkovModel final {
  public:
    using Context = std::vector<std::string>;

    explicit MarkovModel(const std::size_t order, const double alpha = 0.1)
        : order_{order}, alpha_{alpha} {
        if (order_ == 0U) {
            throw std::invalid_argument{"Markov order must be positive"};
        }
        if (!(alpha_ > 0.0) || !std::isfinite(alpha_)) {
            throw std::invalid_argument{
                "Markov smoothing alpha must be finite and positive"};
        }
        vocabulary_.insert(std::string{unknown_token});
        vocabulary_.insert(std::string{end_token});
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
            observe(context, state, weight);
            advance(context, state);
        }
        observe(context, std::string{end_token}, weight);
        training_weight_ += weight;
    }

    [[nodiscard]] double
    log_probability(const std::vector<std::string> &sequence) const {
        if (training_weight_ == 0.0) {
            throw std::logic_error{"Markov model has no training sequences"};
        }
        Context context(order_, std::string{start_token});
        double score{};
        for (const auto &raw_state : sequence) {
            validate_state(raw_state);
            const auto state = normalized(raw_state);
            score += std::log(probability(context, state));
            advance(context, state);
        }
        score += std::log(probability(context, std::string{end_token}));
        return score;
    }

    [[nodiscard]] std::size_t order() const noexcept { return order_; }
    [[nodiscard]] double alpha() const noexcept { return alpha_; }
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
    std::map<Context, Row> transitions_;
    std::set<std::string, std::less<>> vocabulary_;
    double training_weight_{};

    static void validate_state(const std::string_view state) {
        if (state.empty() || state == start_token || state == end_token ||
            state == unknown_token) {
            throw std::invalid_argument{"invalid reserved Markov state"};
        }
    }

    void observe(const Context &context, const std::string &next,
                 const double weight) {
        auto &row = transitions_[context];
        row.next[next] += weight;
        row.total += weight;
    }

    [[nodiscard]] std::string normalized(const std::string &state) const {
        return vocabulary_.contains(state) ? state : std::string{unknown_token};
    }

    [[nodiscard]] double probability(const Context &raw_context,
                                     const std::string &next) const {
        Context context;
        context.reserve(raw_context.size());
        for (const auto &state : raw_context) {
            context.push_back(state == start_token ? state : normalized(state));
        }
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

    static void advance(Context &context, const std::string &state) {
        context.erase(context.begin());
        context.push_back(state);
    }
};

} // namespace parsers
