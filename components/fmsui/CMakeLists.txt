# Builds both as an ESP-IDF component and as a plain CMake library for the
# simulator.  The framework depends on LVGL and on nothing from ESP-IDF, which
# is what lets the two targets share every line of UI code.

set(FMSUI_SRCS
    src/arena.cpp
    src/element.cpp
    src/render.cpp
    src/widgets.cpp
    src/theme.cpp
    src/fms.cpp
    src/app.cpp
)

if(ESP_PLATFORM)
    idf_component_register(
        SRCS ${FMSUI_SRCS}
        INCLUDE_DIRS include
        REQUIRES lvgl
    )
else()
    add_library(fmsui STATIC ${FMSUI_SRCS})
    add_library(fmsui::fmsui ALIAS fmsui)
    target_include_directories(fmsui PUBLIC include)
    target_link_libraries(fmsui PUBLIC lvgl)
    target_compile_features(fmsui PUBLIC cxx_std_20)
    target_compile_options(fmsui PRIVATE -Wall -Wextra)
endif()
