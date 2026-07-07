add_executable(ls_tests
    test_lexer.cpp
    test_parser.cpp
    test_sema.cpp
    test_api.cpp
)

target_link_libraries(ls_tests PRIVATE ls_lib Catch2::Catch2WithMain)
if(MSVC)
    target_compile_options(ls_tests PRIVATE /W4)
else()
    target_compile_options(ls_tests PRIVATE -Wall -Wextra)
endif()

include(CTest)
include(Catch)
catch_discover_tests(ls_tests)
