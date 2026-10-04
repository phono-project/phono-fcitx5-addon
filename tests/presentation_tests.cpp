#include "presentation.h"
#include "config.h"
#include "check.h"

#include <nlohmann/json.hpp>
#include <algorithm>
#include <iostream>

using namespace phono_fcitx;
using nlohmann::json;

namespace {
std::string display(const CandidatePresentation &value) {
    std::string result;
    for (const auto &piece : value.pieces) result += piece.text;
    return result;
}
void checkPresentation(const std::string &candidate,
                       const std::vector<InvalidInput> &invalid,
                       const std::string &shown) {
    const auto pass = presentCandidate(candidate, invalid, InvalidInputPolicy::PassThrough);
    CHECK(display(pass) == shown);
    CHECK(pass.committed == shown);
    for (const auto &piece : pass.pieces) CHECK(!piece.deleted);
    const auto remove = presentCandidate(candidate, invalid, InvalidInputPolicy::Delete);
    CHECK(display(remove) == shown);
    CHECK(remove.committed == candidate);
    std::string kept, deleted;
    for (const auto &piece : remove.pieces) {
        if (piece.deleted) deleted += piece.text;
        else kept += piece.text;
    }
    CHECK(kept == remove.committed);
    std::string expectedDeleted;
    auto sorted = invalid;
    std::stable_sort(sorted.begin(), sorted.end(), [](const auto &a, const auto &b) {
        return a.begin < b.begin;
    });
    for (const auto &range : sorted) expectedDeleted += range.text;
    CHECK(deleted == expectedDeleted);
}
json document(const std::string &input, const std::vector<std::string> &segments,
              json invalid, json events = json::array()) {
    return { {"input", input}, {"segments", segments},
             {"invalid_ranges", std::move(invalid)},
             {"normalization_events", std::move(events)} };
}
void checkRange(const InvalidInput &value, size_t begin, size_t end,
                size_t beforeSyllable, const std::string &text) {
    CHECK(value.begin == begin);
    CHECK(value.end == end);
    CHECK(value.beforeSyllable == beforeSyllable);
    CHECK(value.text == text);
}
} // namespace

int main() {
    try {
        checkPresentation("你好", {}, "你好");
        checkPresentation("你好", {{0, 1, 0, "i"}}, "i你好");
        checkPresentation("你好", {{2, 3, 1, "u"}}, "你u好");
        checkPresentation("你好", {{5, 6, 2, "v"}}, "你好v");
        checkPresentation("你好", {{0, 1, 0, "i"}, {3, 7, 1, "🙂"}, {10, 11, 2, "v"}}, "i你🙂好v");
        checkPresentation("你🙂好", {{2, 5, 1, "字"}}, "你字🙂好");
        checkPresentation("", {{0, 3, 0, "uiv"}}, "uiv");
        // A decoder returning fewer characters still preserves trailing raw
        // input. Spans at the same boundary remain in original source order.
        checkPresentation("你", {{6, 7, 4, "u"}, {2, 3, 1, "#"}, {3, 4, 1, "!"}}, "你#!u");
        const auto struck = presentCandidate("你好", {{2, 3, 1, "u"}}, InvalidInputPolicy::Delete);
        CHECK(struck.pieces.size() == 3);
        CHECK(struck.pieces[0].text == "你" && !struck.pieces[0].deleted);
        CHECK(struck.pieces[1].text == "u" && struck.pieces[1].deleted);
        CHECK(struck.pieces[2].text == "好" && !struck.pieces[2].deleted);

        const auto prefix = invalidFromSegmentation(document("iNI", {"ni"},
            json::array({{{"begin", 0}, {"end", 1}, {"text", "i"}}})));
        CHECK(prefix.size() == 1); checkRange(prefix[0], 0, 1, 0, "i");
        const auto middle = invalidFromSegmentation(document("NIiHAO", {"ni", "hao"},
            json::array({{{"begin", 2}, {"end", 3}, {"text", "i"}}})));
        CHECK(middle.size() == 1); checkRange(middle[0], 2, 3, 1, "i");
        const auto suffix = invalidFromSegmentation(document("NIi", {"ni"},
            json::array({{{"begin", 2}, {"end", 3}, {"text", "i"}}})));
        CHECK(suffix.size() == 1); checkRange(suffix[0], 2, 3, 1, "i");
        const auto allInvalid = invalidFromSegmentation(document("uiv", {},
            json::array({{{"begin", 0}, {"end", 3}, {"text", "uiv"}}})));
        CHECK(allInvalid.size() == 1); checkRange(allInvalid[0], 0, 3, 0, "uiv");

        // Uppercasing and v->u are byte-preserving normalizations. Separators
        // and non-pinyin UTF-8 input are removed when counting token positions.
        const auto normalized = invalidFromSegmentation(document("JVAN'🙂NI#HAO", {"juan", "ni", "hao"},
            json::array({{{"begin", 5}, {"end", 9}, {"text", "🙂"}},
                         {{"begin", 11}, {"end", 12}, {"text", "#"}}}),
            json::array({{{"type", "ascii_lowercase"}, {"begin", 0}, {"end", 4}},
                         {{"type", "v_to_u"}, {"begin", 1}, {"end", 2}},
                         {{"type", "forced_boundary"}, {"begin", 4}, {"end", 5}},
                         {{"type", "invalid_deleted"}, {"begin", 5}, {"end", 9}},
                         {{"type", "invalid_deleted"}, {"begin", 11}, {"end", 12}}})));
        CHECK(normalized.size() == 2);
        checkRange(normalized[0], 5, 9, 1, "🙂");
        checkRange(normalized[1], 11, 12, 2, "#");
        checkPresentation("卷你好", normalized, "卷🙂你#好");
        const auto boundaries = invalidFromSegmentation(document("'ni'#!'hao'v", {"ni", "hao"},
            json::array({{{"begin", 4}, {"end", 6}, {"text", "#!"}},
                         {{"begin", 11}, {"end", 12}, {"text", "v"}}}),
            json::array({{{"type", "forced_boundary"}, {"begin", 0}, {"end", 1}},
                         {{"type", "forced_boundary"}, {"begin", 3}, {"end", 4}},
                         {{"type", "forced_boundary"}, {"begin", 6}, {"end", 7}},
                         {{"type", "forced_boundary"}, {"begin", 10}, {"end", 11}}})));
        CHECK(boundaries.size() == 2);
        checkRange(boundaries[0], 4, 6, 1, "#!");
        checkRange(boundaries[1], 11, 12, 2, "v");
        checkPresentation("你好", boundaries, "你#!好v");

        Config config;
        config.invalidInput.setValue(InvalidInputPolicy::Delete);
        fcitx::RawConfig invalidConfig;
        invalidConfig.setValueByPath("InvalidInput", "Repair");
        config.load(invalidConfig, true);
        CHECK(*config.invalidInput == InvalidInputPolicy::PassThrough);
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
