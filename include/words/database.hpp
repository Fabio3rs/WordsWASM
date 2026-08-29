#pragma once

#include "words/model.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace words {

struct LoadError final {
    std::string code;
    std::string message;
};

struct LexemeRecord final {
    std::array<StringId, 4> stems;
    StringId meaning;
    DictionaryKind dictionary{DictionaryKind::general};
    std::uint32_t dictionary_entry{};
    PartOfSpeech part_of_speech{PartOfSpeech::unknown};
    std::uint8_t declension{};
    std::uint8_t variant{};
    Gender gender{Gender::unknown};
    std::uint8_t noun_kind{};
    PronounKind pronoun_kind{PronounKind::unknown};
    Degree adjective_degree{Degree::unknown};
    NumeralType numeral_type{NumeralType::unknown};
    std::uint16_t numeral_value{};
    Degree adverb_degree{Degree::unknown};
    VerbKind verb_kind{VerbKind::unknown};
    GrammaticalCase governs{GrammaticalCase::unknown};
    std::uint8_t age{};
    std::uint8_t subject{};
    std::uint8_t geography{};
    std::uint8_t frequency{};
    std::uint8_t source{};
};

struct StemReference final {
    LexemeId lexeme;
    std::uint8_t lexical_slot{};
    std::uint8_t stem_key{};
};

struct UniqueReference final {
    LexemeId lexeme;
    Morphology morphology;
};

struct InflectionRule final {
    RuleId id;
    PartOfSpeech part_of_speech{PartOfSpeech::unknown};
    std::uint8_t declension{};
    std::uint8_t variant{};
    GrammaticalCase grammatical_case{GrammaticalCase::unknown};
    GrammaticalNumber number{GrammaticalNumber::unknown};
    Gender gender{Gender::unknown};
    Degree adjective_degree{Degree::unknown};
    NumeralType numeral_type{NumeralType::unknown};
    Tense tense{Tense::unknown};
    Voice voice{Voice::unknown};
    Mood mood{Mood::unknown};
    std::uint8_t person{};
    StringId ending;
    std::uint8_t stem_key{};
    std::uint8_t age{};
    std::uint8_t frequency{};
};

struct SuffixRule final {
    AddonId id;
    SuffixStringId fix;
    SuffixMeaningId meaning;
    PartOfSpeech root{PartOfSpeech::unknown};
    std::uint8_t root_key{};
    PartOfSpeech target{PartOfSpeech::unknown};
    std::uint8_t target_key{};
    std::uint8_t target_declension{};
    std::uint8_t target_variant{};
    Gender target_gender{Gender::unknown};
    std::uint8_t target_noun_kind{};
    Degree target_degree{Degree::unknown};
    NumeralType target_numeral_type{NumeralType::unknown};
    std::uint8_t target_attribute{};
    std::uint8_t numeric_value{};
    char connector{};
};

struct PrefixRule final {
    AddonId id;
    PrefixStringId fix;
    PrefixMeaningId meaning;
    PartOfSpeech root{PartOfSpeech::unknown};
    PartOfSpeech target{PartOfSpeech::unknown};
    char connector{};
};

struct TackonRule final {
    AddonId id;
    TackonStringId fix;
    TackonMeaningId meaning;
    PartOfSpeech base{PartOfSpeech::unknown};
    std::uint8_t declension{};
    std::uint8_t variant{};
    Gender gender{Gender::unknown};
    std::uint8_t noun_kind{};
    PronounKind pronoun_kind{PronounKind::unknown};
    Degree adjective_degree{Degree::unknown};
    bool packon{};
    bool enclitic{};
};

struct RewriteRule final {
    RewriteId id;
    RewriteStringId before;
    RewriteStringId after;
    RewriteStringId name;
    RewriteMeaningId meaning;
    RewriteKind kind{RewriteKind::syncope};
    RewriteScope scope{RewriteScope::internal};
    std::uint8_t priority{};
    bool scan_reverse{};
    PartOfSpeech required_part{PartOfSpeech::unknown};
    std::uint8_t required_stem_key{};
    std::uint8_t minimum_before{};
    std::uint8_t minimum_after{};
    bool medieval{};
    RewriteOperation operation{RewriteOperation::literal};
    RewriteStage stage{RewriteStage::main};
    RewriteConstraint constraint{RewriteConstraint::none};
};

class Database final {
  public:
    [[nodiscard]] static std::expected<std::unique_ptr<const Database>,
                                       LoadError>
    load_dense_poc(std::vector<std::byte> image);

    Database(const Database &) = delete;
    Database &operator=(const Database &) = delete;
    Database(Database &&) = delete;
    Database &operator=(Database &&) = delete;
    ~Database() = default;

    [[nodiscard]] std::span<const StemReference>
    lookup_stem(std::string_view normalized_ascii) const noexcept;

    [[nodiscard]] std::span<const RuleId>
    lookup_ending(std::string_view normalized_ascii) const noexcept;

    [[nodiscard]] std::span<const UniqueReference>
    lookup_unique(std::string_view normalized_ascii) const noexcept;

    [[nodiscard]] std::span<const AddonId>
    lookup_suffix(std::string_view normalized_ascii) const noexcept;

    [[nodiscard]] std::span<const AddonId>
    lookup_prefix(std::string_view normalized_ascii) const noexcept;

    [[nodiscard]] std::span<const AddonId>
    lookup_tickon(std::string_view normalized_ascii) const noexcept;

    [[nodiscard]] std::span<const AddonId>
    lookup_tackon(std::string_view normalized_ascii) const noexcept;

    [[nodiscard]] std::span<const AddonId>
    lookup_packon(std::string_view normalized_ascii) const noexcept;

    [[nodiscard]] const LexemeRecord &lexeme(LexemeId id) const;
    [[nodiscard]] const InflectionRule &rule(RuleId id) const;
    [[nodiscard]] const SuffixRule &suffix(AddonId id) const;
    [[nodiscard]] const PrefixRule &prefix(AddonId id) const;
    [[nodiscard]] const TackonRule &tackon(AddonId id) const;
    [[nodiscard]] const RewriteRule &rewrite(RewriteId id) const;
    [[nodiscard]] std::span<const RewriteRule> rewrites() const noexcept {
        return rewrites_;
    }
    [[nodiscard]] AddonKind addon_kind(AddonId id) const;
    [[nodiscard]] std::string_view stem_string(StringId id) const;
    [[nodiscard]] std::string_view meaning(StringId id) const;
    [[nodiscard]] std::string_view ending_string(StringId id) const;
    [[nodiscard]] std::string_view suffix_string(SuffixStringId id) const;
    [[nodiscard]] std::string_view suffix_meaning(SuffixMeaningId id) const;
    [[nodiscard]] std::string_view prefix_string(PrefixStringId id) const;
    [[nodiscard]] std::string_view prefix_meaning(PrefixMeaningId id) const;
    [[nodiscard]] std::string_view tackon_string(TackonStringId id) const;
    [[nodiscard]] std::string_view tackon_meaning(TackonMeaningId id) const;
    [[nodiscard]] std::string_view rewrite_string(RewriteStringId id) const;
    [[nodiscard]] std::string_view rewrite_meaning(RewriteMeaningId id) const;
    [[nodiscard]] std::size_t maximum_suffix_size() const noexcept {
        return maximum_suffix_size_;
    }
    [[nodiscard]] std::size_t maximum_prefix_size() const noexcept {
        return maximum_prefix_size_;
    }
    [[nodiscard]] std::size_t maximum_tackon_size() const noexcept {
        return maximum_tackon_size_;
    }

  private:
    struct StemGroup final {
        std::string_view key;
        std::uint32_t first{};
        std::uint32_t count{};
    };

    struct EndingGroup final {
        std::string_view key;
        std::uint32_t first{};
        std::uint32_t count{};
    };

    struct UniqueGroup final {
        std::string_view key;
        std::uint32_t first{};
        std::uint32_t count{};
    };

    struct SuffixGroup final {
        std::string_view key;
        std::uint32_t first{};
        std::uint32_t count{};
    };

    struct PrefixGroup final {
        std::string_view key;
        std::uint32_t first{};
        std::uint32_t count{};
    };

    struct TackonGroup final {
        std::string_view key;
        std::uint32_t first{};
        std::uint32_t count{};
    };

    struct AddonReference final {
        AddonKind kind{AddonKind::unknown};
        std::uint32_t ordinal{};
    };

    explicit Database(std::vector<std::byte> image)
        : image_{std::move(image)} {}

    std::vector<std::byte> image_;
    std::vector<std::string_view> stem_strings_;
    std::vector<std::string_view> meaning_strings_;
    std::vector<std::string_view> ending_strings_;
    std::vector<std::string_view> suffix_strings_;
    std::vector<std::string_view> suffix_meanings_;
    std::vector<std::string_view> prefix_strings_;
    std::vector<std::string_view> prefix_meanings_;
    std::vector<std::string_view> tackon_strings_;
    std::vector<std::string_view> tackon_meanings_;
    std::vector<std::string_view> rewrite_strings_;
    std::vector<std::string_view> rewrite_meanings_;
    std::vector<LexemeRecord> lexemes_;
    std::vector<InflectionRule> rules_;
    std::vector<SuffixRule> suffixes_;
    std::vector<PrefixRule> prefixes_;
    std::vector<TackonRule> tackons_;
    std::vector<RewriteRule> rewrites_;
    std::vector<AddonReference> addon_references_;
    std::vector<StemReference> stem_references_;
    std::vector<StemGroup> stem_groups_;
    std::vector<RuleId> ending_rule_ids_;
    std::vector<EndingGroup> ending_groups_;
    std::vector<UniqueReference> unique_references_;
    std::vector<UniqueGroup> unique_groups_;
    std::vector<AddonId> suffix_ids_;
    std::vector<SuffixGroup> suffix_groups_;
    std::vector<AddonId> prefix_ids_;
    std::vector<PrefixGroup> prefix_groups_;
    std::vector<AddonId> tickon_ids_;
    std::vector<PrefixGroup> tickon_groups_;
    std::vector<AddonId> tackon_ids_;
    std::vector<TackonGroup> tackon_groups_;
    std::vector<AddonId> packon_ids_;
    std::vector<TackonGroup> packon_groups_;
    std::size_t maximum_suffix_size_{};
    std::size_t maximum_prefix_size_{};
    std::size_t maximum_tackon_size_{};
};

} // namespace words
