#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>
#include <libime/pinyin/shuangpinprofile.h>

namespace phono_fcitx {

// Offsets always refer to the original UTF-8 byte string, before lowercasing.
struct ShuangpinInvalidSpan {
    size_t begin = 0;
    size_t end = 0;
    bool operator==(const ShuangpinInvalidSpan &) const = default;
};

struct ShuangpinToken {
    size_t begin = 0;
    size_t end = 0;
    size_t syllableIndex = 0;
    // Ordered as in libime's profile table. The first is the canonical reading;
    // a caller may select the first exact reading supported by its model vocab.
    std::vector<std::string> alternatives;
};

struct ShuangpinConversion {
    std::vector<std::string> syllables;
    std::vector<ShuangpinToken> tokens;
    std::vector<ShuangpinInvalidSpan> invalidSpans;
};

// Fixed pairs, not probabilistic segmentation. An illegal pair is split into
// its original single-letter pinyin/initials, never repaired or shifted to form
// another pair. Lone u/i/v are invalid even where a profile maps them to sh/ch/zh.
// Apostrophes and ASCII whitespace delimit pairs and do not produce syllables.
ShuangpinConversion convertShuangpin(std::string_view input,
                                    const libime::ShuangpinProfile &profile);
ShuangpinConversion convertShuangpin(
    std::string_view input, libime::ShuangpinBuiltinProfile profile);
// Resolve strict two-key ambiguity using a native candidate pronunciation,
// only if every syllable matches the same rule-derived alternatives.
void preferShuangpinReading(ShuangpinConversion &, std::string_view fullPinyin);

} // namespace phono_fcitx
