#pragma once
#include <istream>
#include <map>
#include <string>
#include <vector>

namespace phono_fcitx {
class EnglishDictionary {
public:
    void load(std::istream &);
    std::vector<std::string> match(std::string input, size_t minimumLength, int maxDistance = 1) const;
private:
    struct Entry {
        std::string word;
        unsigned tier;
        std::vector<std::pair<int, size_t>> children;
    };
    // Every length bucket remains in lexical order; its middle entry is the
    // root of a metric tree. Edges carry exact Levenshtein distances.
    std::map<size_t, std::vector<Entry>> buckets_;
};
} // namespace phono_fcitx
