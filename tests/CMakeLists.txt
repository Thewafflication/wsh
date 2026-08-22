add_executable(portable-core-tests portable_core_tests.c)
target_include_directories(
    portable-core-tests
    PRIVATE
        "${CMAKE_SOURCE_DIR}/include"
)
target_link_libraries(portable-core-tests PRIVATE wsh_core)

add_executable(parser-tests parser_tests.c)
target_include_directories(
    parser-tests
    PRIVATE
        "${CMAKE_SOURCE_DIR}/include"
)
target_link_libraries(parser-tests PRIVATE wsh_core)
wsh_configure_executable(portable-core-tests)
wsh_configure_executable(parser-tests)

set(WSH_TEST_TOOLCHAIN "TinyCC ${WSH_TINYCC_VERSION}")
set(WSH_TEST_TARGET_ARCHITECTURE "${WSH_TARGET_ARCHITECTURE}")

add_test(NAME version-smoke COMMAND wsh --version)
string(REPLACE "." "\\." WSH_TEST_VERSION_PATTERN "${PROJECT_VERSION}")
set_tests_properties(
    version-smoke
    PROPERTIES
        PASS_REGULAR_EXPRESSION
            "Waughtal Shell.*Version ${WSH_TEST_VERSION_PATTERN}"
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
if(WSH_ENABLE_SOURCE_QUALITY_TESTS)
    find_program(
        WSH_DOXYGEN_EXECUTABLE
        NAMES doxygen
        HINTS "${CMAKE_SOURCE_DIR}/out/tools/doxygen/bin"
        REQUIRED
    )
endif()

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
        -TestImplementationsPath "tests/m2"
)
set_tests_properties(m2-traceability PROPERTIES LABELS "M2;quality")

if(WSH_ENABLE_SOURCE_QUALITY_TESTS)
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
endif()

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
                "${CMAKE_CURRENT_SOURCE_DIR}/m2/run-tc-${test_id}-m2.ps1"
            -TestCase "TC-${test_id}"
            -Executable "$<TARGET_FILE:portable-core-tests>"
            -EvidenceDirectory
                "${CMAKE_BINARY_DIR}/test-evidence/$<CONFIG>/current"
            -RepositoryRoot "${CMAKE_SOURCE_DIR}"
            -Configuration "$<CONFIG>"
            -Toolchain
                "${WSH_TEST_TOOLCHAIN}"
            -TargetArchitecture "${WSH_TEST_TARGET_ARCHITECTURE}"
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

set(WSH_M3_TEST_IDS
    0008
    0009
    0010
    0014
    0015
    0016
    0017
    0023
    0038
    0051
    0052
    0082
    0083
    0084
    0085
)

add_test(
    NAME m3-traceability
    COMMAND
        "${WSH_POWERSHELL_EXECUTABLE}"
        -NoProfile
        -File "${CMAKE_SOURCE_DIR}/wsp/tools/Test-Traceability.ps1"
        -RepositoryRoot "${CMAKE_SOURCE_DIR}"
        -RequirementsPath "docs/requirements/m3"
        -TestSpecificationsPath "docs/tests/m3"
        -TestImplementationsPath "tests/m3"
)
set_tests_properties(m3-traceability PROPERTIES LABELS "M3;quality")

if(WSH_ENABLE_SOURCE_QUALITY_TESTS)
    add_test(
        NAME m3-source-quality
        COMMAND
            "${WSH_POWERSHELL_EXECUTABLE}"
            -NoProfile
            -File "${CMAKE_CURRENT_SOURCE_DIR}/run-m3-source-quality.ps1"
            -RepositoryRoot "${CMAKE_SOURCE_DIR}"
            -Doxygen "${WSH_DOXYGEN_EXECUTABLE}"
    )
    set_tests_properties(
        m3-source-quality
        PROPERTIES
            DEPENDS m3-traceability
            LABELS "M3;quality"
    )
endif()

set(WSH_M3_CTEST_NAMES)
foreach(test_id IN LISTS WSH_M3_TEST_IDS)
    set(test_name "m3-TC-${test_id}")
    list(APPEND WSH_M3_CTEST_NAMES "${test_name}")
    add_test(
        NAME "${test_name}"
        COMMAND
            "${WSH_POWERSHELL_EXECUTABLE}"
            -NoProfile
            -File
                "${CMAKE_CURRENT_SOURCE_DIR}/m3/run-tc-${test_id}-m3.ps1"
            -TestCase "TC-${test_id}"
            -Executable "$<TARGET_FILE:parser-tests>"
            -EvidenceDirectory
                "${CMAKE_BINARY_DIR}/test-evidence/m3/$<CONFIG>/current"
            -RepositoryRoot "${CMAKE_SOURCE_DIR}"
            -Configuration "$<CONFIG>"
            -Toolchain
                "${WSH_TEST_TOOLCHAIN}"
            -TargetArchitecture "${WSH_TEST_TARGET_ARCHITECTURE}"
    )
    set_tests_properties(
        "${test_name}"
        PROPERTIES
            DEPENDS m3-traceability
            LABELS "M3;controlled"
    )
endforeach()

add_test(
    NAME m3-evidence
    COMMAND
        "${WSH_POWERSHELL_EXECUTABLE}"
        -NoProfile
        -File "${CMAKE_SOURCE_DIR}/wsp/tools/Test-TestEvidence.ps1"
        -EvidenceDirectory
            "${CMAKE_BINARY_DIR}/test-evidence/m3/$<CONFIG>/current"
        -ExpectedCount 15
)
set_tests_properties(
    m3-evidence
    PROPERTIES
        DEPENDS "${WSH_M3_CTEST_NAMES}"
        LABELS "M3;quality"
)
