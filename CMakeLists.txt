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

project(fmslikeui)
