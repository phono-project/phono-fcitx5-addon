#pragma once
#include <istream>
#include <map>
#include <string>
#include <vector>

namespace phono_fcitx {
class EnglishDictionary {
public:
    void load(std::istream &);
    std::vector<std::string> match(std::string input, size_t minimumLength) const;
private:
    struct Entry { std::string word; unsigned tier; };
    std::map<size_t, std::vector<Entry>> buckets_;
};
} // namespace phono_fcitx
