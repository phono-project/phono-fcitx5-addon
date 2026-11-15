# SPDX-License-Identifier: Apache-2.0
cmake_minimum_required(VERSION 3.27)
project(phono_macos_libime LANGUAGES CXX)
# Build an ABI-matched, private libime SDK from locked upstream sources.
# Unique install names keep it independent from other Fcitx macOS plugins.
set(BUILD_SHARED_LIBS ON CACHE BOOL "" FORCE)
set(ENABLE_DATA OFF CACHE BOOL "" FORCE)
set(ENABLE_TOOLS OFF CACHE BOOL "" FORCE)
set(ENABLE_TEST OFF CACHE BOOL "" FORCE)
set(CMAKE_CXX_VISIBILITY_PRESET hidden)
set(CMAKE_VISIBILITY_INLINES_HIDDEN ON)
set(CMAKE_MACOSX_RPATH ON)
set(CMAKE_INSTALL_NAME_DIR "@rpath")
set(CMAKE_BUILD_WITH_INSTALL_NAME_DIR ON)
set(CMAKE_INSTALL_RPATH "@loader_path")
add_subdirectory("${LIBIME_SOURCE_DIR}" libime)
set_target_properties(IMECore PROPERTIES OUTPUT_NAME phono_imecore)
set_target_properties(IMEPinyin PROPERTIES OUTPUT_NAME phono_imepinyin)
# Fcitx5CompilerSettings, included inside libime, resets the directory RPATH.
# Apply the installed-library policy after those targets have been created.
set_target_properties(IMECore IMEPinyin PROPERTIES
    INSTALL_RPATH "@loader_path"
    BUILD_WITH_INSTALL_RPATH TRUE
    INSTALL_RPATH_USE_LINK_PATH FALSE)
