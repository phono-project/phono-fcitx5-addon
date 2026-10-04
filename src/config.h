#pragma once
#include "i18n.h"
#include "worker.h"
#include <fcitx-config/configuration.h>
#include <fcitx-config/enum.h>
#include <fcitx-config/option.h>
#include <libime/pinyin/shuangpinprofile.h>
#include <nlohmann/json.hpp>
#include <charconv>
#include <cmath>

namespace phono_fcitx {
FCITX_CONFIG_ENUM_NAME_WITH_I18N(InvalidInputPolicy, N_("Pass through"),
                               N_("Delete (show struck-out text)"));
enum class ShuangpinScheme { Ziranma, MS, Ziguang, ABC, Zhongwenzhixing, PinyinJiajia, Xiaohe };
FCITX_CONFIG_ENUM_NAME_WITH_I18N(ShuangpinScheme, N_("Natural Code"), N_("Microsoft"),
    N_("Purple Light"), N_("Smart ABC"), N_("Chinese Star"), N_("Pinyin Jiajia"), N_("Xiaohe"));
// Fcitx's config protocol provides integers and strings, but no floating type.
// Keep decimal core parameters as validated native string options.
struct DecimalConstrain {
    double minimum = 0;
    double maximum = 1e9;
    bool check(const std::string &text) const {
        double value = 0;
        auto result = std::from_chars(text.data(), text.data() + text.size(), value);
        return result.ec == std::errc() && result.ptr == text.data() + text.size() &&
            std::isfinite(value) && value >= minimum && value <= maximum;
    }
    void dumpDescription(fcitx::RawConfig &) const {}
};
FCITX_CONFIGURATION(
    Config,
    fcitx::Option<bool> enabled{this, "Enabled", _("Enable Phono model"), true};
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
);

inline Settings settingsFor(const Config &config, bool shuangpin = false) {
    using nlohmann::json;
    json context = {{"schema_version", "1.0"}, {"min_accept_context", *config.minAccept},
                    {"trial_ratio", std::stod(*config.trialRatio)}, {"decay_alpha", std::stod(*config.decayAlpha)},
                    {"decay_lambda", std::stod(*config.decayLambda)}};
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
