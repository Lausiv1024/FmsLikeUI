cmake_minimum_required(VERSION 3.16)
project(fmsui_consumer_host LANGUAGES C CXX)

# A plain CMake project that uses FmsLikeUI from source, and nothing else from
# this repository.
#
# It stands where an application would: it brings its own LVGL target, adds
# components/fmsui with add_subdirectory() and links fmsui::fmsui.  The
# top-level CMakeLists.txt, sim/, demo/, tests/ and components/fmsui_fonts are
# not used -- tools/ci/consumer_host.sh builds this from a copy of the tree that
# does not have them, so reaching for any of them fails the configure.
#
# The contract it checks is written down in docs/USING.md.

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Debug)
endif()

# Where FmsLikeUI is.  An application points this at its submodule or its
# vendored copy.
set(FMSUI_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../.. CACHE PATH "FmsLikeUI source tree")

# 1. LVGL belongs to the application.  fmsui links a target called `lvgl`, so
#    that is the name it has to have.  This one is the LVGL and lv_conf.h the
#    framework is verified with.
set(LV_BUILD_CONF_PATH ${FMSUI_DIR}/third_party/lv_conf.h CACHE FILEPATH "" FORCE)
set(CONFIG_LV_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_BUILD_DEMOS OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_USE_THORVG_INTERNAL OFF CACHE BOOL "" FORCE)
add_subdirectory(${FMSUI_DIR}/third_party/lvgl ${CMAKE_BINARY_DIR}/lvgl)

# 2. The framework.
add_subdirectory(${FMSUI_DIR}/components/fmsui ${CMAKE_BINARY_DIR}/fmsui)

# 3. The application.
find_package(Threads REQUIRED)
add_executable(fmsui_consumer main.cpp ../shared/consumer_page.cpp)
target_include_directories(fmsui_consumer PRIVATE ../shared)
target_link_libraries(fmsui_consumer PRIVATE fmsui::fmsui lvgl::lvgl Threads::Threads)

# Every public header, each compiled first and alone in a translation unit of
# its own: the seven for every application, refresh.h and render.h for
# diagnostics and extension, and the umbrella.  A header that only builds
# because something else happened to be included before it fails here.
set(FMSUI_PUBLIC_HEADERS
  app foundation widget widgets theme fms str   # every application
  refresh render                                # diagnostics, extension
  fmsui)                                        # the umbrella
set(header_check_sources)
foreach(FMSUI_HEADER IN LISTS FMSUI_PUBLIC_HEADERS)
  set(tu ${CMAKE_BINARY_DIR}/header_check/${FMSUI_HEADER}.cpp)
  configure_file(header_check.cpp.in ${tu} @ONLY)
  list(APPEND header_check_sources ${tu})
endforeach()
add_library(fmsui_header_check OBJECT ${header_check_sources})
target_link_libraries(fmsui_header_check PRIVATE fmsui::fmsui)

# The headers that are internal to the library, which an application must not
# be able to include.  Each probe is a translation unit that tries, linked the
# way the application is; it is left out of the default build, and
# tools/ci/check_internal_headers.py builds it and requires the compiler to
# report the header as not found.  Not a CTest: the test here is that the build
# fails, and for that one reason.
foreach(FMSUI_HEADER IN ITEMS arena element)
  set(tu ${CMAKE_BINARY_DIR}/internal_header_probe/${FMSUI_HEADER}.cpp)
  configure_file(internal_header_probe.cpp.in ${tu} @ONLY)
  add_library(fmsui_internal_probe_${FMSUI_HEADER} OBJECT EXCLUDE_FROM_ALL ${tu})
  target_link_libraries(fmsui_internal_probe_${FMSUI_HEADER} PRIVATE fmsui::fmsui)
endforeach()

foreach(target fmsui_consumer fmsui_header_check)
  target_compile_options(${target} PRIVATE -Wall -Wextra)
endforeach()

# Warnings as errors in this project's own code, which includes every public
# header it compiles.  fmsui's own sources are held to that by the builds in
# sim/ and the device project; LVGL is not held to it at all.
option(FMSUI_WERROR "Treat warnings in the consumer's own targets as errors" OFF)
if(FMSUI_WERROR)
  foreach(target fmsui_consumer fmsui_header_check)
    target_compile_options(${target} PRIVATE -Werror)
  endforeach()
endif()

enable_testing()
add_test(NAME fmsui_consumer COMMAND fmsui_consumer)
set_tests_properties(fmsui_consumer PROPERTIES TIMEOUT 60)
