add_executable(portable-core-tests portable_core_tests.c)
target_include_directories(
    portable-core-tests
    PRIVATE
        "${CMAKE_SOURCE_DIR}/include"
)
target_link_libraries(portable-core-tests PRIVATE wsh_core)

if(MSVC)
    target_compile_options(portable-core-tests PRIVATE /W4 /WX)
else()
    target_compile_options(
        portable-core-tests
        PRIVATE
            -Wall
            -Wextra
            -Wpedantic
            -Werror
    )
endif()

add_test(NAME version-smoke COMMAND wsh --version)
set_tests_properties(
    version-smoke
    PROPERTIES
        PASS_REGULAR_EXPRESSION "Waughtal Shell.*Version 1\\.0\\.0"
)

add_test(NAME print-abi-smoke COMMAND wsh --print-abi)
set_tests_properties(
    print-abi-smoke
    PROPERTIES
        PASS_REGULAR_EXPRESSION "wsh embedding ABI 1"
)

find_program(
    WSH_POWERSHELL_EXECUTABLE
    NAMES pwsh
    REQUIRED
)
find_program(
    WSH_DOXYGEN_EXECUTABLE
    NAMES doxygen
    HINTS "${CMAKE_SOURCE_DIR}/out/tools/doxygen/bin"
    REQUIRED
)

set(WSH_M2_TEST_IDS
    0007
    0018
    0019
    0024
    0034
    0035
    0036
    0037
    0046
    0048
    0049
    0070
    0074
    0075
)

add_test(
    NAME m2-traceability
    COMMAND
        "${WSH_POWERSHELL_EXECUTABLE}"
        -NoProfile
        -File "${CMAKE_SOURCE_DIR}/wsp/tools/Test-Traceability.ps1"
        -RepositoryRoot "${CMAKE_SOURCE_DIR}"
        -RequirementsPath "docs/requirements/m2"
        -TestSpecificationsPath "docs/tests/m2"
        -TestImplementationsPath "tests"
)
set_tests_properties(m2-traceability PROPERTIES LABELS "M2;quality")

add_test(
    NAME m2-source-quality
    COMMAND
        "${WSH_POWERSHELL_EXECUTABLE}"
        -NoProfile
        -File "${CMAKE_CURRENT_SOURCE_DIR}/run-m2-source-quality.ps1"
        -RepositoryRoot "${CMAKE_SOURCE_DIR}"
        -Doxygen "${WSH_DOXYGEN_EXECUTABLE}"
)
set_tests_properties(
    m2-source-quality
    PROPERTIES
        DEPENDS m2-traceability
        LABELS "M2;quality"
)

set(WSH_M2_CTEST_NAMES)
foreach(test_id IN LISTS WSH_M2_TEST_IDS)
    set(test_name "m2-TC-${test_id}")
    list(APPEND WSH_M2_CTEST_NAMES "${test_name}")
    add_test(
        NAME "${test_name}"
        COMMAND
            "${WSH_POWERSHELL_EXECUTABLE}"
            -NoProfile
            -File
                "${CMAKE_CURRENT_SOURCE_DIR}/run-tc-${test_id}-m2.ps1"
            -TestCase "TC-${test_id}"
            -Executable "$<TARGET_FILE:portable-core-tests>"
            -EvidenceDirectory
                "${CMAKE_BINARY_DIR}/test-evidence/$<CONFIG>/current"
            -RepositoryRoot "${CMAKE_SOURCE_DIR}"
            -Configuration "$<CONFIG>"
            -Toolchain
                "${CMAKE_C_COMPILER_ID} ${CMAKE_C_COMPILER_VERSION}"
            -TargetArchitecture "${CMAKE_GENERATOR_PLATFORM}"
    )
    set_tests_properties(
        "${test_name}"
        PROPERTIES
            DEPENDS m2-traceability
            LABELS "M2;controlled"
    )
endforeach()

add_test(
    NAME m2-evidence
    COMMAND
        "${WSH_POWERSHELL_EXECUTABLE}"
        -NoProfile
        -File "${CMAKE_SOURCE_DIR}/wsp/tools/Test-TestEvidence.ps1"
        -EvidenceDirectory
            "${CMAKE_BINARY_DIR}/test-evidence/$<CONFIG>/current"
        -ExpectedCount 14
)
set_tests_properties(
    m2-evidence
    PROPERTIES
        DEPENDS "${WSH_M2_CTEST_NAMES}"
        LABELS "M2;quality"
)
