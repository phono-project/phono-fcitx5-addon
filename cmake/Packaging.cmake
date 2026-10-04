# SPDX-License-Identifier: Apache-2.0
# Included after the install rules. Native distro recipes use the normal
# DESTDIR install; CPack is a reproducible archive/local-package convenience.
include_guard(GLOBAL)
set(PHONO_PACKAGE_GENERATORS "TGZ" CACHE STRING "CPack generators (TGZ;DEB;RPM)")
set(PHONO_PACKAGE_CONTACT "Local package builder <root@localhost>" CACHE STRING
    "Package builder contact; downstream releases should set their own contact")

install(FILES "${PROJECT_SOURCE_DIR}/LICENSE"
    DESTINATION "${CMAKE_INSTALL_DATADIR}/licenses/${PROJECT_NAME}")
install(FILES "${PROJECT_SOURCE_DIR}/README.md"
    DESTINATION "${CMAKE_INSTALL_DOCDIR}")
install(DIRECTORY "${PROJECT_SOURCE_DIR}/docs/" DESTINATION "${CMAKE_INSTALL_DOCDIR}/docs"
    FILES_MATCHING PATTERN "*.md")
if(PHONO_BUNDLE_CORE)
    set(PHONO_CORE_LICENSE_ROOT "${PHONO_CORE_ROOT}" CACHE PATH
        "Core source/license tree used for bundled redistribution notices")
    set(PHONO_CORE_EXTRA_LICENSE_FILES "" CACHE STRING
        "Additional semicolon-separated license/notice files for bundled core dependencies")
    # Preserve each origin directory: most dependencies name their file LICENSE,
    # and flattening them would silently overwrite different copyright notices.
    foreach(_phono_notice
        LICENSE
        third_party/executorch/LICENSE
        third_party/executorch/backends/xnnpack/third-party/XNNPACK/LICENSE
        third_party/executorch/backends/xnnpack/third-party/pthreadpool/LICENSE
        third_party/executorch/backends/xnnpack/third-party/cpuinfo/LICENSE
        third_party/executorch/backends/xnnpack/third-party/FP16/LICENSE
        third_party/executorch/backends/xnnpack/third-party/FXdiv/LICENSE
        third_party/executorch/third-party/flatbuffers/LICENSE
        third_party/executorch/third-party/flatcc/LICENSE
        third_party/executorch/third-party/gflags/COPYING.txt
        third_party/executorch/third-party/json/LICENSE.MIT
        third_party/uni-algo/LICENSE.md)
        if(EXISTS "${PHONO_CORE_LICENSE_ROOT}/${_phono_notice}")
            get_filename_component(_phono_notice_dir "${_phono_notice}" DIRECTORY)
            install(FILES "${PHONO_CORE_LICENSE_ROOT}/${_phono_notice}"
                DESTINATION "${CMAKE_INSTALL_DATADIR}/licenses/${PROJECT_NAME}/phono-core/${_phono_notice_dir}")
        endif()
    endforeach()
    if(NOT EXISTS "${PHONO_CORE_LICENSE_ROOT}/LICENSE")
        message(WARNING "Bundled redistribution needs PHONO_CORE_LICENSE_ROOT with the core LICENSE and dependency notices")
    endif()
    set(_phono_extra_notice_index 0)
    foreach(_phono_notice IN LISTS PHONO_CORE_EXTRA_LICENSE_FILES)
        get_filename_component(_phono_notice_name "${_phono_notice}" NAME)
        install(FILES "${_phono_notice}"
            DESTINATION "${CMAKE_INSTALL_DATADIR}/licenses/${PROJECT_NAME}/phono-core/extra"
            RENAME "${_phono_extra_notice_index}-${_phono_notice_name}")
        math(EXPR _phono_extra_notice_index "${_phono_extra_notice_index} + 1")
    endforeach()
endif()

set(CPACK_PACKAGE_NAME "fcitx5-phono")
set(CPACK_PACKAGE_VENDOR "Phono Project")
set(CPACK_PACKAGE_CONTACT "${PHONO_PACKAGE_CONTACT}")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Phono Pinyin and Shuangpin input methods for Fcitx5")
set(CPACK_PACKAGE_HOMEPAGE_URL "https://github.com/phono-project")
set(CPACK_RESOURCE_FILE_LICENSE "${PROJECT_SOURCE_DIR}/LICENSE")
set(CPACK_GENERATOR "${PHONO_PACKAGE_GENERATORS}")
set(CPACK_PACKAGE_FILE_NAME "${CPACK_PACKAGE_NAME}-${PROJECT_VERSION}-${CMAKE_SYSTEM_PROCESSOR}")
set(CPACK_INCLUDE_TOPLEVEL_DIRECTORY OFF)
# Respect /usr, lib64 and multiarch paths from the actual target's configure.
# Never rewrite absolute Fcitx paths into a nested /usr/usr layout.
set(CPACK_SET_DESTDIR ON)
set(CPACK_PACKAGING_INSTALL_PREFIX "${CMAKE_INSTALL_PREFIX}")

set(CPACK_DEBIAN_PACKAGE_SECTION "utils")
set(CPACK_DEBIAN_PACKAGE_PRIORITY "optional")
set(CPACK_DEBIAN_PACKAGE_HOMEPAGE "${CPACK_PACKAGE_HOMEPAGE_URL}")
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
set(CPACK_DEBIAN_PACKAGE_DEPENDS "fcitx5 (>= 5.1.7), libime-data, libime-data-language-model, fcitx5-module-punctuation, fcitx5-module-fullwidth")
set(CPACK_RPM_PACKAGE_LICENSE "Apache-2.0")
set(CPACK_RPM_PACKAGE_URL "${CPACK_PACKAGE_HOMEPAGE_URL}")
set(CPACK_RPM_PACKAGE_GROUP "System/I18n/Chinese")
set(CPACK_RPM_PACKAGE_AUTOREQPROV ON)
set(CPACK_RPM_PACKAGE_RELOCATABLE OFF)
# These shared directories belong to the distribution's filesystem/Fcitx
# packages. Owning them here can conflict with their native permissions.
set(CPACK_RPM_EXCLUDE_FROM_AUTO_FILELIST_ADDITION
    "/usr/share/licenses" "/usr/share/locale"
    "/usr/share/fcitx5" "/usr/share/fcitx5/addon" "/usr/share/fcitx5/inputmethod"
    "${CMAKE_INSTALL_PREFIX}/${PHONO_FCITX_ADDON_DIR}")
foreach(_phono_language zh_CN zh_TW zh_HK)
    list(APPEND CPACK_RPM_EXCLUDE_FROM_AUTO_FILELIST_ADDITION
        "/usr/share/locale/${_phono_language}" "/usr/share/locale/${_phono_language}/LC_MESSAGES")
endforeach()
set(_phono_rpm_data_default "libime-data")
if(EXISTS "/etc/os-release")
    file(READ "/etc/os-release" _phono_os_release)
    if(_phono_os_release MATCHES "(opensuse|sles|sled)")
        set(_phono_rpm_data_default "libime-dicts")
    endif()
endif()
set(PHONO_RPM_PINYIN_DATA_PACKAGE "${_phono_rpm_data_default}" CACHE STRING
    "RPM package providing sc.dict and the libime language model")
set(CPACK_RPM_PACKAGE_REQUIRES "fcitx5 >= 5.1.7, ${PHONO_RPM_PINYIN_DATA_PACKAGE}, fcitx5-chinese-addons")
if(PHONO_BUNDLE_CORE)
    # The notices above cover the source tree's known core dependencies. Custom
    # core builds must add notices for any additional compiled dependencies.
    # The verifier explicitly rejects development RPATHs in copied DSOs.
    set(CPACK_PACKAGE_DESCRIPTION "In-process asynchronous neural Pinyin and rule-based Shuangpin for Fcitx5. The phono-core shared library is bundled; model data is supplied separately.")
    set(CPACK_RPM_PACKAGE_LICENSE "Apache-2.0 AND BSD-3-Clause AND BSD-2-Clause AND MIT")
else()
    string(APPEND CPACK_DEBIAN_PACKAGE_DEPENDS ", phono-core")
    string(APPEND CPACK_RPM_PACKAGE_REQUIRES ", phono-core")
    set(CPACK_PACKAGE_DESCRIPTION "In-process asynchronous neural Pinyin and rule-based Shuangpin for Fcitx5. Requires a separately packaged phono-core shared library; model data is supplied separately.")
endif()
include(CPack)
