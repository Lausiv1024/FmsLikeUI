idf_component_register(
    SRCS
        "main.cpp"
        "../demo/m0_probe.cpp"
        "../demo/m1_demo.cpp"
        "../demo/catalog.cpp"
        "../demo/fms_pages.cpp"
        "../demo/reorder_demo.cpp"
        "../demo/fplan_demo.cpp"
    INCLUDE_DIRS
        "."
        "../demo"
    REQUIRES
        fmsui
        fmsui_fonts
        lvgl
        esp_timer
)

# idf.py -DFMSUI_DEMO=m0 build  -> the raw-LVGL probe screen instead of the framework
if(FMSUI_DEMO STREQUAL "m0")
    target_compile_definitions(${COMPONENT_LIB} PRIVATE FMSUI_DEMO_M0=1)
elseif(FMSUI_DEMO STREQUAL "m1")
    target_compile_definitions(${COMPONENT_LIB} PRIVATE FMSUI_DEMO_M1=1)
elseif(FMSUI_DEMO STREQUAL "catalog")
    target_compile_definitions(${COMPONENT_LIB} PRIVATE FMSUI_DEMO_CATALOG=1)
elseif(FMSUI_DEMO STREQUAL "fplan")
    target_compile_definitions(${COMPONENT_LIB} PRIVATE FMSUI_DEMO_FPLAN=1)
elseif(FMSUI_DEMO STREQUAL "reorder")
    # -DFMSUI_ROWS=40 puts the page at the same lv_obj count as ACTIVE/PERF +
    # ACTIVE/INIT, which is the scale the reorder cost has to be judged at.
    if(NOT DEFINED FMSUI_ROWS)
        set(FMSUI_ROWS 6)
    endif()
    target_compile_definitions(${COMPONENT_LIB} PRIVATE
        FMSUI_DEMO_REORDER=1 FMSUI_REORDER_ROWS=${FMSUI_ROWS})
endif()
