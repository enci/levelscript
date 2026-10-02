add_executable(levelscript main.cpp)
target_link_libraries(levelscript PRIVATE ls_lib)
if(MSVC)
    target_compile_options(levelscript PRIVATE /W4)
else()
    target_compile_options(levelscript PRIVATE -Wall -Wextra)
endif()

# ── levelscript-debugger (the interactive ImGui debugger) ─────────────────────────────────

# Native title-bar tinting is platform-specific; pick the matching implementation.
if(APPLE)
    set(LS_PLATFORM_SRC platform_titlebar_mac.mm)
elseif(WIN32)
    set(LS_PLATFORM_SRC platform_titlebar_win.cpp)
else()
    set(LS_PLATFORM_SRC platform_titlebar_other.cpp)
endif()

add_executable(levelscript-debugger debugger_main.cpp debug_ui.cpp debug_ui_views.cpp
    project_config.cpp stb_image.cpp
    ${LS_PLATFORM_SRC}
    ${imgui_SOURCE_DIR}/misc/freetype/imgui_freetype.cpp)
target_link_libraries(levelscript-debugger PRIVATE ls_lib imgui_lib nlohmann_json::nlohmann_json freetype)
target_include_directories(levelscript-debugger PRIVATE
    ${stb_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/misc/freetype
    ${freetype_SOURCE_DIR}/include)

if(APPLE)
    target_link_libraries(levelscript-debugger PRIVATE "-framework Cocoa")
elseif(WIN32)
    target_link_libraries(levelscript-debugger PRIVATE dwmapi)
endif()
