add_executable(lsc main.cpp)
target_link_libraries(lsc PRIVATE ls_lib)
if(MSVC)
    target_compile_options(lsc PRIVATE /W4)
else()
    target_compile_options(lsc PRIVATE -Wall -Wextra)
endif()

# ── lsd (the interactive ImGui debugger) ──────────────────────────────────────

# Native title-bar tinting is platform-specific; pick the matching implementation.
if(APPLE)
    set(LS_PLATFORM_SRC platform_titlebar_mac.mm)
elseif(WIN32)
    set(LS_PLATFORM_SRC platform_titlebar_win.cpp)
else()
    set(LS_PLATFORM_SRC platform_titlebar_other.cpp)
endif()

add_executable(lsd lsd_main.cpp debug_ui.cpp debug_ui_views.cpp
    project_config.cpp stb_image.cpp
    ${LS_PLATFORM_SRC}
    ${imgui_SOURCE_DIR}/misc/freetype/imgui_freetype.cpp)
target_link_libraries(lsd PRIVATE ls_lib imgui_lib nlohmann_json::nlohmann_json freetype)
target_include_directories(lsd PRIVATE
    ${stb_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/misc/freetype
    ${freetype_SOURCE_DIR}/include)

if(APPLE)
    target_link_libraries(lsd PRIVATE "-framework Cocoa")
elseif(WIN32)
    target_link_libraries(lsd PRIVATE dwmapi)
endif()
