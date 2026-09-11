cmake_minimum_required(VERSION 3.16)

# third_party/ holds our pinned LVGL.  Registering it as a component directory
# makes it override the `lvgl` that esp_lvgl_port would otherwise pull from the
# component registry, so the device and the simulator compile the same sources.
set(EXTRA_COMPONENT_DIRS ${CMAKE_CURRENT_LIST_DIR}/third_party)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# third_party/lv_conf.h is the single source of truth for LVGL's configuration
# on both targets.  Ignore LVGL's Kconfig entirely so menuconfig cannot quietly
# introduce a second, conflicting one.
idf_build_set_property(COMPILE_DEFINITIONS "LV_KCONFIG_IGNORE" APPEND)

# idf.py -DFMSUI_WERROR=ON build  -> a warning in the code this project owns
# fails the build.  CI turns it on; main, fmsui and fmsui_fonts append these
# after ESP-IDF's own options.
#
# ESP-IDF already compiles with -Werror=all, but then hands -Wextra and the
# unused-* family back with -Wno-error=<name>, and a later bare -Werror does not
# undo a -Wno-error=<name>.  So each of those is named again.  Nothing here
# reaches ESP-IDF, the managed components or LVGL: they keep ESP-IDF's policy.
set(FMSUI_WERROR_FLAGS
    -Werror
    -Werror=unused-function
    -Werror=unused-variable
    -Werror=unused-but-set-variable
    -Werror=deprecated-declarations
    -Werror=extra
)

project(fmslikeui)
