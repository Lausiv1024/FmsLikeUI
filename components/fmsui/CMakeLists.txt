# Builds both as an ESP-IDF component and as a plain CMake library for the
# simulator.  The framework depends on LVGL and on nothing from ESP-IDF, which
# is what lets the two targets share every line of UI code.
#
# include/ is the public API.  src/internal/ holds the headers only the library
# is built with -- the arena and the element tree -- and is given privately:
# PRIVATE here, PRIV_INCLUDE_DIRS for ESP-IDF.  They keep their fmsui/ prefix on
# purpose.  If that directory ever reached an application, <fmsui/element.h>
# would start compiling there, and that is exactly what the consumer check
# (tools/ci/check_internal_headers.py) tries and expects to fail.

set(FMSUI_SRCS
    src/arena.cpp
    src/element.cpp
    src/render.cpp
    src/widgets.cpp
    src/theme.cpp
    src/fms.cpp
    src/app.cpp
    src/refresh.cpp
)

if(ESP_PLATFORM)
    idf_component_register(
        SRCS ${FMSUI_SRCS}
        INCLUDE_DIRS include
        PRIV_INCLUDE_DIRS src/internal
        REQUIRES lvgl
    )
    if(FMSUI_WERROR)
        target_compile_options(${COMPONENT_LIB} PRIVATE ${FMSUI_WERROR_FLAGS})
    endif()
else()
    add_library(fmsui STATIC ${FMSUI_SRCS})
    add_library(fmsui::fmsui ALIAS fmsui)
    target_include_directories(fmsui PUBLIC include PRIVATE src/internal)
    target_link_libraries(fmsui PUBLIC lvgl)
    target_compile_features(fmsui PUBLIC cxx_std_20)
    target_compile_options(fmsui PRIVATE -Wall -Wextra)
endif()
