#pragma once
#include "worker.h"
#include <nlohmann/json_fwd.hpp>

namespace phono_fcitx {
struct CandidatePiece {
    std::string text;
    bool deleted = false;
};
struct CandidatePresentation {
    std::vector<CandidatePiece> pieces;
    std::string committed;
};
// Map the core's source-byte diagnostics to generated syllable positions.
std::vector<InvalidInput> invalidFromSegmentation(const nlohmann::json &document);
CandidatePresentation presentCandidate(const std::string &decoded,
    const std::vector<InvalidInput> &, InvalidInputPolicy);
} // namespace phono_fcitx
