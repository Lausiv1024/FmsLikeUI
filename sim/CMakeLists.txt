cmake_minimum_required(VERSION 3.16)
project(fmsui_sim LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Debug)
endif()

set(REPO_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/..)

# Both targets read the same lv_conf.h the device build uses.
# (LVGL 9.5 renamed these from LV_CONF_PATH / LV_CONF_BUILD_DISABLE_*.)
set(LV_BUILD_CONF_PATH ${REPO_ROOT}/third_party/lv_conf.h CACHE FILEPATH "" FORCE)
set(CONFIG_LV_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_BUILD_DEMOS OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_USE_THORVG_INTERNAL OFF CACHE BOOL "" FORCE)
add_subdirectory(${REPO_ROOT}/third_party/lvgl ${CMAKE_BINARY_DIR}/lvgl)

add_subdirectory(${REPO_ROOT}/components/fmsui ${CMAKE_BINARY_DIR}/fmsui)
add_subdirectory(${REPO_ROOT}/components/fmsui_fonts ${CMAKE_BINARY_DIR}/fmsui_fonts)

find_package(SDL2 REQUIRED)

add_executable(fmsui_sim
  main_sim.cpp
  ${REPO_ROOT}/demo/m0_probe.cpp
  ${REPO_ROOT}/demo/m1_demo.cpp
  ${REPO_ROOT}/demo/catalog.cpp
  ${REPO_ROOT}/demo/fms_pages.cpp
  ${REPO_ROOT}/demo/reorder_demo.cpp
  ${REPO_ROOT}/demo/fplan_demo.cpp
)

target_include_directories(fmsui_sim PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${REPO_ROOT}/demo
  ${SDL2_INCLUDE_DIRS}
)

target_link_libraries(fmsui_sim PRIVATE fmsui fmsui_fonts lvgl ${SDL2_LIBRARIES})

# The simulator is where memory bugs in the Widget arena should surface, long
# before they turn into a hard fault on the device.
option(FMSUI_SANITIZE "Build the simulator with ASan/UBSan" OFF)
if(FMSUI_SANITIZE)
  # Instrument the framework itself as well as the executable that calls it.
  # fmsui is a static library, so it needs compile instrumentation but the
  # sanitizer runtime is linked by each executable below.
  target_compile_options(fmsui PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
  target_compile_options(fmsui_sim PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
  target_link_options(fmsui_sim PRIVATE -fsanitize=address,undefined)
endif()

target_compile_options(fmsui_sim PRIVATE -Wall -Wextra)

# Layout maths and reconciliation, tested headlessly.
enable_testing()
add_executable(fmsui_test ${REPO_ROOT}/tests/fmsui_test.cpp)
target_link_libraries(fmsui_test PRIVATE fmsui lvgl)
target_compile_options(fmsui_test PRIVATE -Wall -Wextra)
if(FMSUI_SANITIZE)
  target_compile_options(fmsui_test PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
  target_link_options(fmsui_test PRIVATE -fsanitize=address,undefined)
endif()
add_test(NAME fmsui_test COMMAND fmsui_test)

# Input, tested headlessly through a real LVGL pointer device.
#
# A separate executable from fmsui_test rather than more cases inside it: this
# one owns an LVGL display, an input device and the FmsApp singleton, and
# keeping that state out of the layout tests is what makes a failure here read
# as "the input chain broke" and not as something about layout.  It links the
# real fonts because the controls it taps are laid out with them.
add_executable(fmsui_interaction_test ${REPO_ROOT}/tests/fmsui_interaction_test.cpp)
target_link_libraries(fmsui_interaction_test PRIVATE fmsui fmsui_fonts lvgl)
target_compile_options(fmsui_interaction_test PRIVATE -Wall -Wextra)
if(FMSUI_SANITIZE)
  target_compile_options(fmsui_interaction_test PRIVATE
    -fsanitize=address,undefined -fno-omit-frame-pointer)
  target_link_options(fmsui_interaction_test PRIVATE -fsanitize=address,undefined)
endif()
add_test(NAME fmsui_interaction_test COMMAND fmsui_interaction_test)

# requestFrame() under many requests and several producer threads, through the
# real FmsApp frame loop.
#
# Separate again, because this is the one that can hang: its threads meet at
# condition variables, and a request path that ever waited on the build would
# leave them waiting forever.  The timeout is what turns that into a failure --
# the tests finish in seconds, so a minute is a stuck run, not a slow one.
find_package(Threads REQUIRED)
add_executable(fmsui_request_frame_test ${REPO_ROOT}/tests/fmsui_request_frame_test.cpp)
target_link_libraries(fmsui_request_frame_test PRIVATE fmsui lvgl Threads::Threads)
target_compile_options(fmsui_request_frame_test PRIVATE -Wall -Wextra)
if(FMSUI_SANITIZE)
  target_compile_options(fmsui_request_frame_test PRIVATE
    -fsanitize=address,undefined -fno-omit-frame-pointer)
  target_link_options(fmsui_request_frame_test PRIVATE -fsanitize=address,undefined)
endif()
add_test(NAME fmsui_request_frame_test COMMAND fmsui_request_frame_test)
set_tests_properties(fmsui_request_frame_test PROPERTIES TIMEOUT 60)

# Warnings as errors, on the targets this project owns; CI turns it on.  LVGL is
# left out on purpose: its warnings are LVGL's to fix, and a compiler that finds
# a new one there should not be able to turn the framework's build red.
option(FMSUI_WERROR "Treat warnings in project-owned targets as errors" OFF)
if(FMSUI_WERROR)
  foreach(target fmsui fmsui_fonts fmsui_sim
                 fmsui_test fmsui_interaction_test fmsui_request_frame_test)
    target_compile_options(${target} PRIVATE -Werror)
  endforeach()
endif()
