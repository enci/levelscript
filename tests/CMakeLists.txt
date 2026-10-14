add_executable(ls_tests
    test_lexer.cpp
    test_parser.cpp
    test_sema.cpp
    test_api.cpp
    test_inspect.cpp
    test_sequence.cpp
    test_modules.cpp
)

target_link_libraries(ls_tests PRIVATE ls_lib Catch2::Catch2WithMain)
target_compile_definitions(ls_tests PRIVATE LS_AST_GOLDEN_DIR="${CMAKE_CURRENT_SOURCE_DIR}/ast")
if(MSVC)
    target_compile_options(ls_tests PRIVATE /W4)
else()
    target_compile_options(ls_tests PRIVATE -Wall -Wextra)
endif()

include(CTest)
include(Catch)
catch_discover_tests(ls_tests)
