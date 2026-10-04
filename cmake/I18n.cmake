include_guard(GLOBAL)
include(GNUInstallDirs)
find_package(Gettext REQUIRED)

add_library(phono_i18n INTERFACE)
target_compile_definitions(phono_i18n INTERFACE
    PHONO_LOCALEDIR="${CMAKE_INSTALL_FULL_LOCALEDIR}")

set(PHONO_TRANSLATION_OUTPUTS)
foreach(PHONO_LANGUAGE zh_CN zh_TW)
    set(PHONO_MO "${CMAKE_CURRENT_BINARY_DIR}/locale/${PHONO_LANGUAGE}/LC_MESSAGES/phono-fcitx5-addon.mo")
    add_custom_command(OUTPUT "${PHONO_MO}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory
            "${CMAKE_CURRENT_BINARY_DIR}/locale/${PHONO_LANGUAGE}/LC_MESSAGES"
        COMMAND "${GETTEXT_MSGFMT_EXECUTABLE}" --check --check-format
            -o "${PHONO_MO}" "${CMAKE_CURRENT_SOURCE_DIR}/po/${PHONO_LANGUAGE}.po"
        DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/po/${PHONO_LANGUAGE}.po"
        VERBATIM)
    list(APPEND PHONO_TRANSLATION_OUTPUTS "${PHONO_MO}")
    install(FILES "${PHONO_MO}"
        DESTINATION "${CMAKE_INSTALL_LOCALEDIR}/${PHONO_LANGUAGE}/LC_MESSAGES")
endforeach()
# Hong Kong uses the same Traditional Chinese catalog.
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/locale/zh_TW/LC_MESSAGES/phono-fcitx5-addon.mo"
    DESTINATION "${CMAKE_INSTALL_LOCALEDIR}/zh_HK/LC_MESSAGES")
add_custom_target(phono_translations ALL DEPENDS ${PHONO_TRANSLATION_OUTPUTS})

