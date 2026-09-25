add_library(ls_lib STATIC
    lexer.cpp
    parser.cpp
    sema.cpp
    machine.cpp
    ls.cpp
    inspect.cpp
)

target_include_directories(ls_lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
if(MSVC)
    target_compile_options(ls_lib PRIVATE /W4)
else()
    target_compile_options(ls_lib PRIVATE -Wall -Wextra -Wpedantic)
endif()

# ── WASM module for the VS Code extension ─────────────────────────────────────
# Build with:  emcmake cmake -B build-wasm && cmake --build build-wasm
# Emits ls_wasm.js + ls_wasm.wasm straight into extension/ (gitignored).
if(EMSCRIPTEN)
    add_executable(ls_wasm wasm_bind.cpp)
    target_link_libraries(ls_wasm PRIVATE ls_lib embind)
    target_link_options(ls_wasm PRIVATE
        -sMODULARIZE=1
        -sEXPORT_NAME=createLsModule
        -sENVIRONMENT=node
        -sALLOW_MEMORY_GROWTH=1
        -fexceptions)
    target_compile_options(ls_wasm PRIVATE -fexceptions)
    target_compile_options(ls_lib PRIVATE -fexceptions)
    set_target_properties(ls_wasm PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/extension)
endif()
