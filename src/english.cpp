#include "english.h"
#include <algorithm>
#include <sstream>

namespace phono_fcitx {
void EnglishDictionary::load(std::istream &stream) {
    buckets_.clear();
    std::string line, word;
    unsigned tier;
    while (std::getline(stream, line)) {
        std::istringstream record(line);
        if (!(record >> word >> tier) || word.size() > 40 ||
            !std::all_of(word.begin(), word.end(), [](char c) { return c >= 'a' && c <= 'z'; })) continue;
        buckets_[word.size()].push_back({word, tier});
    }
    for (auto &[length, entries] : buckets_)
        std::sort(entries.begin(), entries.end(), [](const auto &a, const auto &b) { return a.word < b.word; });
}
std::vector<std::string> EnglishDictionary::match(std::string input, size_t minimumLength) const {
    if (input.size() < minimumLength || input.size() > 40) return {};
    const auto original = input;
    for (char &c : input) {
        if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
        if (c < 'a' || c > 'z') return {};
    }
    const auto bucket = buckets_.find(input.size());
    if (bucket == buckets_.end()) return {};
    const auto &entries = bucket->second;
    const auto found = std::lower_bound(entries.begin(), entries.end(), input,
        [](const auto &entry, const auto &word) { return entry.word < word; });
    return found != entries.end() && found->word == input ? std::vector{original} : std::vector<std::string>{};
}
} // namespace phono_fcitx
