#include "shuangpin.h"
#include "check.h"

#include <algorithm>
#include <array>
#include <iostream>

using namespace phono_fcitx;
using Profile = libime::ShuangpinBuiltinProfile;

int main() {
    try {
        struct Example { Profile profile; const char *hello; const char *china; };
        const std::array<Example, 7> examples{{
            {Profile::Ziranma, "nihkma", "vsgo"},
            {Profile::MS, "nihkma", "vsgo"},
            {Profile::Ziguang, "nihqma", "uhgo"},
            {Profile::ABC, "nihkma", "asgo"},
            {Profile::Zhongwenzhixing, "nihdma", "vygo"},
            {Profile::PinyinJiajia, "nihdma", "vygo"},
            {Profile::Xiaohe, "nihcma", "vsgo"},
        }};
        for (const auto &example : examples) {
            const libime::ShuangpinProfile profile(example.profile);
            const auto hello = convertShuangpin(example.hello, profile);
            CHECK((hello.syllables == std::vector<std::string>{"ni", "hao", "ma"}));
            CHECK(hello.invalidSpans.empty());
            CHECK(hello.tokens.size() == 3);
            for (size_t i = 0; i < hello.tokens.size(); ++i) {
                CHECK(hello.tokens[i].begin == i * 2);
                CHECK(hello.tokens[i].end == i * 2 + 2);
                CHECK(hello.tokens[i].syllableIndex == i);
                CHECK(hello.tokens[i].alternatives.front() == hello.syllables[i]);
            }
            const auto china = convertShuangpin(example.china, profile);
            CHECK((china.syllables == std::vector<std::string>{"zhong", "guo"}));
            // All profiles preserve the actual two-key ambiguity; callers must
            // not treat an ambiguous code as an arbitrary unrecognized string.
            const auto ambiguous = convertShuangpin("lo", profile);
            CHECK((ambiguous.tokens[0].alternatives ==
                   std::vector<std::string>{"lo", "luo"}));
            CHECK(ambiguous.syllables[0] == "lo");


        }
        const auto jiajia = convertShuangpin("er", Profile::PinyinJiajia);
        CHECK((jiajia.tokens[0].alternatives == std::vector<std::string>{"en", "er"}));
        auto ambiguous = convertShuangpin("loni", Profile::Xiaohe);
        preferShuangpinReading(ambiguous, "luo'ni");
        CHECK(ambiguous.tokens[0].alternatives.front() == "luo");
        CHECK(ambiguous.syllables[0] == "luo");
        auto abbreviation = convertShuangpin("fp", Profile::Xiaohe);
        preferShuangpinReading(abbreviation, "fei'peng");
        CHECK((abbreviation.syllables == std::vector<std::string>{"f", "p"}));
        auto mismatched = convertShuangpin("loni", Profile::Xiaohe);
        preferShuangpinReading(mismatched, "luo");
        CHECK(mismatched.tokens[0].alternatives.front() == "lo");
        const auto separated = convertShuangpin("A'E O\tNI\nHC", Profile::Xiaohe);
        CHECK((separated.syllables == std::vector<std::string>{"a", "e", "o", "ni", "hao"}));
        CHECK(separated.tokens[3].begin == 6);
        CHECK(separated.tokens[3].end == 8);
        CHECK(separated.tokens[4].begin == 9);
        CHECK(separated.tokens[4].end == 11);
        CHECK(separated.invalidSpans.empty());

        const auto split = convertShuangpin("fpniq", Profile::Xiaohe);
        CHECK((split.syllables == std::vector<std::string>{"f", "p", "ni", "q"}));
        CHECK(split.tokens[0].begin == 0 && split.tokens[0].end == 1);
        CHECK(split.tokens[1].begin == 1 && split.tokens[1].end == 2);
        CHECK(split.tokens[2].begin == 2 && split.tokens[2].end == 4);
        CHECK(split.tokens[3].begin == 4 && split.tokens[3].end == 5);
        const auto invalid = convertShuangpin("u'i'v", Profile::Xiaohe);
        CHECK(invalid.syllables.empty());
        CHECK((invalid.invalidSpans == std::vector<ShuangpinInvalidSpan>{{0, 1}, {2, 3}, {4, 5}}));
        const auto badpair = convertShuangpin("ibni", Profile::Xiaohe);
        CHECK((badpair.syllables == std::vector<std::string>{"b", "ni"}));
        CHECK((badpair.invalidSpans == std::vector<ShuangpinInvalidSpan>{{0, 1}}));
        // MS and Ziguang use semicolon as the ing final. It is not punctuation
        // when it completes a valid pair; a lone semicolon stays invalid.
        CHECK((convertShuangpin("n;", Profile::MS).syllables == std::vector<std::string>{"ning"}));
        CHECK((convertShuangpin("n;", Profile::Ziguang).syllables == std::vector<std::string>{"ning"}));
        CHECK((convertShuangpin(";", Profile::MS).invalidSpans == std::vector<ShuangpinInvalidSpan>{{0, 1}}));
        const auto utf8 = convertShuangpin("ni🙂hc!", Profile::Xiaohe);
        CHECK((utf8.syllables == std::vector<std::string>{"ni", "hao"}));
        CHECK((utf8.invalidSpans == std::vector<ShuangpinInvalidSpan>{{2, 6}, {8, 9}}));
        CHECK(utf8.tokens[1].begin == 6 && utf8.tokens[1].end == 8);
        CHECK(convertShuangpin("", Profile::Xiaohe).syllables.empty());
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
