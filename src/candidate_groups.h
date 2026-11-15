#pragma once
#include <vector>

namespace phono_fcitx {
// Sources register in policy order. Appending to either side is stable and
// relative to the complete model group, never to a beam-size-dependent index.
template <typename T> struct CandidateGroups {
    std::vector<T> before, model, after;
    void append(bool first, const std::vector<T> &items) {
        auto &destination = first ? before : after;
        destination.insert(destination.end(), items.begin(), items.end());
    }
};
} // namespace phono_fcitx
