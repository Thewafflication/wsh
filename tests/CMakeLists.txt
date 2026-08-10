enable_testing()

add_executable(portable-core-tests tests/portable_core_tests.c)
target_include_directories(portable-core-tests PRIVATE "${CMAKE_SOURCE_DIR}/include")
target_link_libraries(portable-core-tests PRIVATE wsh_core)

add_test(NAME version-smoke COMMAND wsh --version)
set_tests_properties(version-smoke PROPERTIES
    PASS_REGULAR_EXPRESSION "Waughtal Shell.*Version 1\\.0\\.0"
)

add_test(NAME print-abi-smoke COMMAND wsh --print-abi)
set_tests_properties(print-abi-smoke PROPERTIES
    PASS_REGULAR_EXPRESSION "wsh embedding ABI 1"
)

add_test(NAME portable-core-tests COMMAND portable-core-tests)
