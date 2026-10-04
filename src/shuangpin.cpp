#include "shuangpin.h"

#include <algorithm>

namespace phono_fcitx {
namespace {
char lowercase(char c) { return c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c; }
bool key(char c) { return (c >= 'a' && c <= 'z') || c == ';'; }
bool separator(char c) {
    return c == '\'' || c == ' ' || c == '\t' || c == '\r' || c == '\n' ||
           c == '\f' || c == '\v';
}
bool single(char c) {
    // These are the ordinary pinyin initial letters plus the three valid
    // one-letter zero-initial syllables. Model-specific vocabulary membership
    // remains an exact lookup at the core boundary.
    constexpr std::string_view valid = "abcdefghjklmnopqrstwxyz";
    return valid.find(c) != std::string_view::npos;
}

std::vector<std::string> readings(std::string_view pair,
                                  const libime::ShuangpinProfile &profile) {
    const auto found = profile.table().find(std::string(pair));
    if (found == profile.table().end()) return {};
    std::vector<std::string> result;
    for (const auto &[syllable, flags] : found->second) {
        // Do not enable spelling corrections, fuzzy finals or incomplete pairs.
        if (flags != libime::PinyinFuzzyFlag::None ||
            syllable.initial() == libime::PinyinInitial::Invalid ||
            syllable.final() == libime::PinyinFinal::Invalid) continue;
        auto spelling = syllable.toString();
        if (!spelling.empty() &&
            std::find(result.begin(), result.end(), spelling) == result.end()) {
            result.push_back(std::move(spelling));
        }
    }
    return result;
}

void append(ShuangpinConversion &out, size_t begin, size_t end,
            std::vector<std::string> alternatives) {
    const size_t index = out.syllables.size();
    out.syllables.push_back(alternatives.front());
    out.tokens.push_back({begin, end, index, std::move(alternatives)});
}
void invalid(ShuangpinConversion &out, size_t begin, size_t end) {
    // Merge adjacent invalid bytes, but not across explicit separators or valid
    // syllables, so the frontend can preserve their location in the candidate.
    if (!out.invalidSpans.empty() && out.invalidSpans.back().end == begin) {
        out.invalidSpans.back().end = end;
    } else {
        out.invalidSpans.push_back({begin, end});
    }
}
void split(ShuangpinConversion &out, std::string_view input, size_t offset) {
    const char c = lowercase(input[offset]);
    if (single(c)) append(out, offset, offset + 1, {std::string(1, c)});
    else invalid(out, offset, offset + 1);
}
} // namespace

ShuangpinConversion convertShuangpin(std::string_view input,
                                    const libime::ShuangpinProfile &profile) {
    ShuangpinConversion result;
    for (size_t offset = 0; offset < input.size();) {
        const char first = lowercase(input[offset]);
        if (separator(first)) { ++offset; continue; }
        if (!key(first)) {
            // Non-ASCII input is invalid here; contiguous UTF-8 bytes form one
            // span and are retained verbatim by the frontend's pass-through.
            invalid(result, offset, offset + 1);
            ++offset;
            continue;
        }
        if (offset + 1 < input.size() && key(lowercase(input[offset + 1]))) {
            const std::string pair{first, lowercase(input[offset + 1])};
            auto alternatives = readings(pair, profile);
            if (!alternatives.empty()) {
                append(result, offset, offset + 2, std::move(alternatives));
            } else {
                split(result, input, offset);
                split(result, input, offset + 1);
            }
            offset += 2;
        } else {
            split(result, input, offset++);
        }
    }
    return result;
}

ShuangpinConversion convertShuangpin(
    std::string_view input, libime::ShuangpinBuiltinProfile profile) {
    return convertShuangpin(input, libime::ShuangpinProfile(profile));
}
void preferShuangpinReading(ShuangpinConversion &converted, std::string_view fullPinyin) {
    if (!converted.invalidSpans.empty()) return;
    std::vector<std::string> syllables;
    for (size_t begin = 0; begin < fullPinyin.size();) {
        const auto end = fullPinyin.find('\'', begin);
        syllables.emplace_back(fullPinyin.substr(begin, end == fullPinyin.npos ? end : end - begin));
        if (end == fullPinyin.npos) break;
        begin = end + 1;
    }
    if (syllables.size() != converted.tokens.size()) return;
    for (size_t i = 0; i < syllables.size(); ++i) {
        const auto &alternatives = converted.tokens[i].alternatives;
        if (std::find(alternatives.begin(), alternatives.end(), syllables[i]) == alternatives.end()) return;
    }
    for (size_t i = 0; i < syllables.size(); ++i) {
        auto &alternatives = converted.tokens[i].alternatives;
        const auto preferred = std::find(alternatives.begin(), alternatives.end(), syllables[i]);
        std::rotate(alternatives.begin(), preferred, preferred + 1);
        converted.syllables[i] = alternatives.front();
    }
}
} // namespace phono_fcitx
