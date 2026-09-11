cmake_minimum_required(VERSION 3.16)

# An ESP-IDF project that uses FmsLikeUI as a local component, and nothing else
# from this repository: not the top-level project, main/, demo/, the Tab5 BSP or
# components/fmsui_fonts.  tools/ci/consumer_idf.sh builds it from a copy of the
# tree that does not have them.  It is built, never flashed.
#
# The contract it checks is written down in docs/USING.md.

get_filename_component(FMSUI_DIR ${CMAKE_CURRENT_LIST_DIR}/../.. ABSOLUTE)

# The framework, and LVGL at the version and lv_conf.h it is verified with.
# components/fmsui is named by itself rather than components/, so fmsui_fonts
# does not come along with it.  LVGL's component finds lv_conf.h in the
# directory above it.
set(EXTRA_COMPONENT_DIRS
    ${FMSUI_DIR}/components/fmsui
    ${FMSUI_DIR}/third_party/lvgl
)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# Take LVGL's configuration from lv_conf.h, not from LVGL's Kconfig.
idf_build_set_property(COMPILE_DEFINITIONS "LV_KCONFIG_IGNORE" APPEND)

# idf.py -DFMSUI_WERROR=ON build -> a warning in fmsui or in main fails the build.
# The fmsui component appends FMSUI_WERROR_FLAGS when FMSUI_WERROR is on, and
# takes the list from the project that includes it; a project that sets neither
# builds fmsui with ESP-IDF's own warning policy.  Why the list names each
# warning again: see FMSUI_WERROR_FLAGS in the top-level CMakeLists.txt.
set(FMSUI_WERROR_FLAGS
    -Werror
    -Werror=unused-function
    -Werror=unused-variable
    -Werror=unused-but-set-variable
    -Werror=deprecated-declarations
    -Werror=extra
)

project(fmsui_consumer)
