#pragma once
#include "i18n.h"
#include "worker.h"
#include <fcitx-config/configuration.h>
#include <fcitx-config/enum.h>
#include <fcitx-config/option.h>
#include <libime/pinyin/shuangpinprofile.h>
#include <nlohmann/json.hpp>
#include <locale>
#include <optional>
#include <sstream>
#include <cmath>

namespace phono_fcitx {
FCITX_CONFIG_ENUM_NAME_WITH_I18N(InvalidInputPolicy, N_("Pass through"),
                               N_("Delete (show struck-out text)"));
enum class ShuangpinScheme { Ziranma, MS, Ziguang, ABC, Zhongwenzhixing, PinyinJiajia, Xiaohe };
FCITX_CONFIG_ENUM_NAME_WITH_I18N(ShuangpinScheme, N_("Natural Code"), N_("Microsoft"),
    N_("Purple Light"), N_("Smart ABC"), N_("Chinese Star"), N_("Pinyin Jiajia"), N_("Xiaohe"));
// Fcitx's config protocol provides integers and strings, but no floating type.
// Keep decimal core parameters as validated native string options.
// Classic-locale streams work with the system libc++ on macOS 14, where
// floating-point from_chars is not available. Use the same parser for JSON.
inline std::optional<double> parseDecimal(const std::string &text) {
    if (text.empty() || text.front() == '+') return {};
    std::istringstream stream(text);
    stream.imbue(std::locale::classic());
    double value = 0;
    stream >> std::noskipws >> value;
    if (stream.fail() || stream.peek() != std::char_traits<char>::eof() || !std::isfinite(value)) return {};
    return value;
}
struct DecimalConstrain {
    double minimum = 0;
    double maximum = 1e9;
    bool check(const std::string &text) const {
        const auto value = parseDecimal(text);
        return value && *value >= minimum && *value <= maximum;
    }
    void dumpDescription(fcitx::RawConfig &) const {}
};
FCITX_CONFIGURATION(
    EnglishConfig,
    fcitx::Option<bool> enabled{this, "Enabled", _("Enable English words"), true};
    fcitx::Option<int, fcitx::IntConstrain> minLength{this, "MinimumLength", _("Minimum English input length"), 4, {1, 40}};
    fcitx::Option<int, fcitx::IntConstrain> maxDistance{this, "MaximumDistance", _("Maximum English edit distance"), 1, {0, 3}};
    fcitx::Option<bool> first{this, "BeforeModel", _("Show English words before model results"), true};
);
FCITX_CONFIGURATION(
    DictionaryConfig,
    fcitx::Option<EnglishConfig> english{this, "English", _("English dictionary"), {}};
    fcitx::HiddenOption<std::vector<std::string>> sourceOrder{this, "SourceOrder", "", {"user", "english"}};
);
FCITX_CONFIGURATION(
    Config,
    fcitx::Option<DictionaryConfig> dictionary{this, "DefaultDictionary", _("Default dictionary"), {}};
    fcitx::Option<bool> enabled{this, "Enabled", _("Enable Phono model"), true};
    fcitx::Option<bool> showPreedit{this, "ShowPreedit", _("Show preedit"), false};
    fcitx::Option<bool> preferUnigram{this, "PreferUnigramSingleSyllable", _("Use unigram probabilities for a single syllable without context"), true};
    fcitx::Option<std::string> model{this, "ModelDirectory", _("Model package directory"), ""};
    fcitx::Option<int, fcitx::IntConstrain> pageSize{this, "PageSize", _("Candidates per page"), 7, {3, 10}};
    fcitx::Option<int, fcitx::IntConstrain> contextSlots{this, "ContextSlots", _("Context cache slots"), 4, {1, 32}};
    fcitx::Option<bool> surrounding{this, "UseSurroundingText", _("Use surrounding text from applications"), true};
    fcitx::Option<int, fcitx::IntConstrain> historyChars{this, "SurroundingCharacters", _("Maximum surrounding text characters"), 256, {0, 8192}};
    fcitx::Option<bool> lowercase{this, "LowercaseASCII", _("Normalize ASCII case"), true};
    fcitx::Option<bool> normalizeV{this, "NormalizeVToU", _("Normalize v after j/q/x/y"), true};
    fcitx::Option<std::string> separators{this, "Separators", _("Explicit syllable separators"), "'"};
    fcitx::OptionWithAnnotation<InvalidInputPolicy, InvalidInputPolicyI18NAnnotation> invalidInput{
        this, "InvalidInput", _("Invalid input"), InvalidInputPolicy::PassThrough};
    fcitx::OptionWithAnnotation<ShuangpinScheme, ShuangpinSchemeI18NAnnotation> shuangpin{
        this, "ShuangpinScheme", _("Shuangpin scheme"), ShuangpinScheme::Xiaohe};
    fcitx::Option<int, fcitx::IntConstrain> maxChars{this, "MaxPinyinCharacters", _("Maximum pinyin characters"), 128, {1, 4096}};
    fcitx::Option<int, fcitx::IntConstrain> maxContext{this, "MaxContextLength", _("Context length (0: automatic)"), 0, {0, 8192}};
    fcitx::Option<int, fcitx::IntConstrain> maxHistory{this, "MaxHistoryLength", _("History length (0: automatic)"), 0, {0, 8192}};
    fcitx::Option<int, fcitx::IntConstrain> maxPinyin{this, "MaxPinyinLength", _("Pinyin syllables (0: automatic)"), 0, {0, 8192}};
    fcitx::Option<int, fcitx::IntConstrain> slack{this, "SlackInterval", _("History window recycling interval (-1: automatic)"), -1, {-1, 8192}};
    fcitx::Option<int, fcitx::IntConstrain> minAccept{this, "MinAcceptContext", _("Minimum context length to reuse"), 8, {1, 8192}};
    fcitx::Option<std::string, DecimalConstrain> trialRatio{this, "TrialRatio", _("Context trial ratio (0 < x <= 1)"), "0.25", {1e-9, 1}};
    fcitx::Option<std::string, DecimalConstrain> decayAlpha{this, "DecayAlpha", _("Cache decay factor (decimal)"), "0.5", {}};
    fcitx::Option<std::string, DecimalConstrain> decayLambda{this, "DecayLambda", _("Cache time decay factor (decimal)"), "0.02", {}};
    fcitx::Option<bool> learning{this, "Learning", _("Learn from dictionary selections"), true};
    fcitx::Option<bool> userWordsExactOnly{this, "UserWordsExactOnly", _("Prioritize user words only for exact matches"), true};
    fcitx::Option<bool> userWordsEnabled{this, "UserWordsEnabled", _("Enable user words"), true};
    fcitx::Option<bool> userWordsFirst{this, "UserWordsFirst", _("Show user words before model results"), true};
);

inline Settings settingsFor(const Config &config, bool shuangpin = false) {
    using nlohmann::json;
    json context = {{"schema_version", "1.0"}, {"min_accept_context", *config.minAccept},
                    {"trial_ratio", parseDecimal(*config.trialRatio).value()}, {"decay_alpha", parseDecimal(*config.decayAlpha).value()},
                    {"decay_lambda", parseDecimal(*config.decayLambda).value()}};
    if (*config.maxContext) context["max_context_length"] = *config.maxContext;
    if (*config.maxHistory) context["max_history_length"] = *config.maxHistory;
    if (*config.maxPinyin) context["max_pinyin_length"] = *config.maxPinyin;
    if (*config.slack >= 0) context["slack_interval"] = *config.slack;
    // beam_size is a model batch dimension, not an independent runtime knob.
    json engine = {{"schema_version", "1.0"}, {"tokenizer", {
        {"normalization", {{"lowercase_ascii", *config.lowercase},
                           {"normalize_v_to_u", *config.normalizeV},
                           {"separators", *config.separators}}},
        {"segment_mode", "safe"}, {"use_segmenter", !shuangpin},
        {"repair", false}, {"max_pinyin_chars", *config.maxChars}}}};
    return {*config.model, engine.dump(), context.dump(), *config.contextSlots};
}
} // namespace phono_fcitx
