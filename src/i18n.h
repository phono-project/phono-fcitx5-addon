#pragma once

#include <fcitx-utils/i18n.h>
#include <filesystem>

#ifndef PHONO_LOCALEDIR
#define PHONO_LOCALEDIR "/usr/share/locale"
#endif

namespace phono_fcitx {
inline constexpr const char *gettextDomain = "phono-fcitx5-addon";

template <class Directory>
inline void registerTranslationDirectory(const Directory &directory) {
    if constexpr (requires { fcitx::registerDomain(gettextDomain, directory); }) {
        fcitx::registerDomain(gettextDomain, directory);
    } else {
        // Fcitx before the filesystem API migration takes a UTF-8 C string.
        fcitx::registerDomain(gettextDomain, directory.string().c_str());
    }
}

// Bind before the first description is translated: Config is a member of
// Engine and is constructed before the Engine constructor body runs.
inline void initializeI18n(const std::filesystem::path &directory = PHONO_LOCALEDIR) {
    static const bool registered = [&directory] {
        registerTranslationDirectory(directory);
        return true;
    }();
    (void)registered;
}

inline const char *translate(const char *source) {
    initializeI18n();
    return fcitx::translateDomain(gettextDomain, source);
}
inline std::string translate(const std::string &source) {
    initializeI18n();
    return fcitx::translateDomain(gettextDomain, source);
}
} // namespace phono_fcitx

// Fcitx's enum annotations call _. Keep them in this addon's domain as well.
#ifdef _
#undef _
#endif
#define _(...) ::phono_fcitx::translate(__VA_ARGS__)
