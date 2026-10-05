cmake_minimum_required(VERSION 3.20)
project(levelscript CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Emscripten builds only produce the VS Code extension's WASM module (see
# src/CMakeLists.txt); skip the debugger/test dependencies entirely.
if(EMSCRIPTEN)
    add_subdirectory(src)
    return()
endif()

include(FetchContent)

# ── levelscript-debugger dependencies: SDL3 + ImGui + JSON + stb + FreeType ─────────
set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON  CACHE BOOL "" FORCE)
set(SDL_TEST   OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    sdl3
    GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
    GIT_TAG        main
)
FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG        docking
)
set(JSON_BuildTests OFF CACHE BOOL "" FORCE)
set(JSON_Install    OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG        v3.11.3
)
FetchContent_Declare(
    stb
    GIT_REPOSITORY https://github.com/nothings/stb.git
    GIT_TAG        master
)
set(FT_DISABLE_ZLIB     ON CACHE BOOL "" FORCE)
set(FT_DISABLE_BZIP2    ON CACHE BOOL "" FORCE)
set(FT_DISABLE_PNG      ON CACHE BOOL "" FORCE)
set(FT_DISABLE_HARFBUZZ ON CACHE BOOL "" FORCE)
set(FT_DISABLE_BROTLI   ON CACHE BOOL "" FORCE)
FetchContent_Declare(
    freetype
    GIT_REPOSITORY https://github.com/freetype/freetype.git
    GIT_TAG        master
)
FetchContent_MakeAvailable(sdl3 imgui nlohmann_json stb freetype)

# Opt-in patch to ImGui's docking branch: stock ImGui only rounds the corners
# of a docked pane that touch the dock host's outer edge, so inner panes stay
# square regardless of WindowRounding. The patch rounds every docked pane.
# Applied at configure time and kept in sync with the option (idempotent).
option(LS_IMGUI_ROUND_DOCKED "Patch ImGui so docked panes get rounded corners" OFF)
set(_ls_imgui_patch ${CMAKE_CURRENT_SOURCE_DIR}/cmake/imgui-round-docked.patch)
find_package(Git QUIET)
if(GIT_FOUND)
    execute_process(COMMAND ${GIT_EXECUTABLE} apply --check --reverse ${_ls_imgui_patch}
        WORKING_DIRECTORY ${imgui_SOURCE_DIR} RESULT_VARIABLE _ls_patched ERROR_QUIET OUTPUT_QUIET)
    if(LS_IMGUI_ROUND_DOCKED AND NOT _ls_patched EQUAL 0)
        execute_process(COMMAND ${GIT_EXECUTABLE} apply ${_ls_imgui_patch}
            WORKING_DIRECTORY ${imgui_SOURCE_DIR} RESULT_VARIABLE _ls_rc)
        if(NOT _ls_rc EQUAL 0)
            message(FATAL_ERROR "LS_IMGUI_ROUND_DOCKED: patch does not apply to this ImGui revision")
        endif()
    elseif(NOT LS_IMGUI_ROUND_DOCKED AND _ls_patched EQUAL 0)
        execute_process(COMMAND ${GIT_EXECUTABLE} apply --reverse ${_ls_imgui_patch}
            WORKING_DIRECTORY ${imgui_SOURCE_DIR})
    endif()
elseif(LS_IMGUI_ROUND_DOCKED)
    message(FATAL_ERROR "LS_IMGUI_ROUND_DOCKED needs git to apply the ImGui patch")
endif()

# Off by default: imgui_demo.cpp (and the debugger's "Demo" toggle) are a dev
# tool for exploring ImGui widgets, not something normal builds should ship.
option(LS_ENABLE_IMGUI_DEMO "Build in ImGui's demo window (dev tool)" ON)

add_library(imgui_lib STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/misc/cpp/imgui_stdlib.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_sdlrenderer3.cpp
)
if(LS_ENABLE_IMGUI_DEMO)
    target_sources(imgui_lib PRIVATE ${imgui_SOURCE_DIR}/imgui_demo.cpp)
    target_compile_definitions(imgui_lib PUBLIC LS_ENABLE_IMGUI_DEMO)
endif()
target_include_directories(imgui_lib PUBLIC
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/misc/cpp
    ${imgui_SOURCE_DIR}/backends
)
target_compile_definitions(imgui_lib PUBLIC IMGUI_USE_WCHAR32)
target_link_libraries(imgui_lib PUBLIC SDL3::SDL3-static)

add_subdirectory(src)
add_subdirectory(app)

FetchContent_Declare(
    Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG        v3.5.3
)
FetchContent_MakeAvailable(Catch2)
list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)

enable_testing()
add_subdirectory(tests)
