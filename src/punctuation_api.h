#pragma once
#include <fcitx/addoninstance.h>
#include <fcitx/inputcontext.h>
#include <string>
#include <utility>

// Public Fcitx5 Chinese Addons ABI, kept here so building the adapter does not
// depend on any private pinyin-addon headers. The runtime module owns mapping,
// paired punctuation, Latin/number exceptions, and its native configuration.
FCITX_ADDON_DECLARE_FUNCTION(Punctuation, pushPunctuationV2,
    std::pair<std::string, std::string>(const std::string &, fcitx::InputContext *, uint32_t));
