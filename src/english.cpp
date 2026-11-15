#include "english.h"
#include <algorithm>
#include <array>
#include <numeric>
#include <optional>
#include <sstream>
#include <tuple>

namespace phono_fcitx {
namespace {
int distance(const std::string &a, const std::string &b) {
    // Both dictionary words and accepted queries are at most 40 ASCII letters.
    std::array<int, 41> previous{}, current{};
    std::iota(previous.begin(), previous.end(), 0);
    for (size_t i = 1; i <= a.size(); ++i) {
        current[0] = i;
        for (size_t j = 1; j <= b.size(); ++j)
            current[j] = std::min({current[j - 1] + 1, previous[j] + 1,
                previous[j - 1] + (a[i - 1] != b[j - 1])});
        previous = current;
    }
    return previous[b.size()];
}
}
void EnglishDictionary::load(std::istream &stream) {
    buckets_.clear();
    std::string line, word;
    unsigned tier;
    while (std::getline(stream, line)) {
        std::istringstream record(line);
        if (!(record >> word >> tier) || word.size() > 40 ||
            !std::all_of(word.begin(), word.end(), [](char c) { return c >= 'a' && c <= 'z'; })) continue;
        buckets_[word.size()].push_back({word, tier, {}});
    }
    for (auto &[length, entries] : buckets_) {
        std::sort(entries.begin(), entries.end(), [](const auto &a, const auto &b) {
            return std::tie(a.word, a.tier) < std::tie(b.word, b.tier);
        });
        entries.erase(std::unique(entries.begin(), entries.end(), [](const auto &a, const auto &b) {
            return a.word == b.word;
        }), entries.end());
        const auto root = entries.size() / 2;
        // Median-first insertion avoids the degenerate lexical insertion order
        // while preserving the sorted storage used by exact lookups.
        const auto insertRange = [&](auto &&self, size_t begin, size_t end) -> void {
            if (begin == end) return;
            const size_t index = (begin + end) / 2;
            if (index != root) {
                auto node = root;
                while (true) {
                    const auto edge = distance(entries[node].word, entries[index].word);
                    auto &children = entries[node].children;
                    auto found = std::find_if(children.begin(), children.end(), [edge](const auto &child) {
                        return child.first == edge;
                    });
                    if (found == children.end()) { children.emplace_back(edge, index); break; }
                    node = found->second;
                }
            }
            self(self, begin, index);
            self(self, index + 1, end);
        };
        insertRange(insertRange, 0, entries.size());
    }
}
std::vector<std::string> EnglishDictionary::match(std::string input, size_t minimumLength, int maxDistance) const {
    if (input.empty() || input.size() < minimumLength || input.size() > 40 || maxDistance < 0 || maxDistance > 3) return {};
    const auto original = input;
    for (char &c : input) {
        if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
        if (c < 'a' || c > 'z') return {};
    }
    if (const auto bucket = buckets_.find(input.size()); bucket != buckets_.end()) {
        const auto &entries = bucket->second;
        const auto found = std::lower_bound(entries.begin(), entries.end(), input,
            [](const auto &entry, const auto &word) { return entry.word < word; });
        if (found != entries.end() && found->word == input) return {original};
    }
    if (!maxDistance) return {};
    std::optional<std::tuple<int, unsigned, std::string>> best;
    const auto length = static_cast<int>(input.size());
    for (const auto &[wordLength, entries] : buckets_) {
        // The difference in lengths is itself a lower bound on edit distance.
        if (std::abs(static_cast<int>(wordLength) - length) > maxDistance) continue;
        std::vector<size_t> pending{entries.size() / 2};
        while (!pending.empty()) {
            const auto &entry = entries[pending.back()]; pending.pop_back();
            const auto d = distance(input, entry.word);
            const auto rank = std::tuple(d, entry.tier, entry.word);
            if (d <= maxDistance && (!best || rank < *best)) best = rank;
            const int radius = best ? std::get<0>(*best) : maxDistance;
            for (const auto &[edge, child] : entry.children)
                // Triangle inequality: no descendant on an edge outside this
                // interval can be within radius of the query. Keep ties for
                // commonness and lexical ranking.
                if (std::abs(edge - d) <= radius) pending.push_back(child);
        }
    }
    if (!best) return {};
    auto result = std::get<2>(*best);
    const bool upper = std::all_of(original.begin(), original.end(), [](char c) { return c >= 'A' && c <= 'Z'; });
    if (upper) for (auto &c : result) c -= 'a' - 'A';
    else if (original.front() >= 'A' && original.front() <= 'Z') result.front() -= 'a' - 'A';
    return {result};
}
} // namespace phono_fcitx
