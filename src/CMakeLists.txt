add_library(ls_lib STATIC
    lexer.cpp
    parser.cpp
    sema.cpp
    machine.cpp
    ls.cpp
)

target_include_directories(ls_lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
if(MSVC)
    target_compile_options(ls_lib PRIVATE /W4)
else()
    target_compile_options(ls_lib PRIVATE -Wall -Wextra -Wpedantic)
endif()
