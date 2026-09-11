idf_component_register(
    SRCS
        "main.cpp"
        "../../shared/consumer_page.cpp"
    INCLUDE_DIRS
        "../../shared"
    REQUIRES
        fmsui
        lvgl
        esp_timer
)

# -DFMSUI_WERROR=ON: see the project CMakeLists.txt.
if(FMSUI_WERROR)
    target_compile_options(${COMPONENT_LIB} PRIVATE ${FMSUI_WERROR_FLAGS})
endif()
