#pragma once
template<typename Word> bool candidateCommentEmpty(const Word &word) {
    if constexpr (requires { word.comment(); }) return word.comment().empty();
    else return true; // Older Fcitx has no candidate comment field.
}
#include <stdexcept>
#include <string>
#define CHECK(condition) do { if (!(condition)) throw std::runtime_error( \
    std::string(__FILE__) + ":" + std::to_string(__LINE__) + ": " #condition); } while (false)
