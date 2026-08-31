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

add_executable(evaluator-tests evaluator_tests.c)
add_executable(runtime-probe helpers/runtime_probe.c)
add_executable(console-driver helpers/console_driver.c)
target_link_libraries(console-driver PRIVATE WCRT::WCRT)
add_executable(windows-runtime-tests windows_runtime_tests.c)
target_include_directories(
    evaluator-tests
    PRIVATE
        "${CMAKE_SOURCE_DIR}/include"
)
target_link_libraries(evaluator-tests PRIVATE wsh_core)
target_include_directories(
    runtime-probe
    PRIVATE
        "${CMAKE_SOURCE_DIR}/include"
)
target_link_libraries(runtime-probe PRIVATE wsh_core)
target_include_directories(
    windows-runtime-tests
    PRIVATE
        "${CMAKE_SOURCE_DIR}/include"
)
target_link_libraries(
    windows-runtime-tests
    PRIVATE
        wsh_windows_runtime
        wsh_core
)

add_executable(abi-conformance-tests abi_conformance_tests.c)
target_include_directories(
    abi-conformance-tests
    PRIVATE
        "${CMAKE_SOURCE_DIR}/include"
)
target_link_libraries(abi-conformance-tests PRIVATE wsh_static)

# Every public embedding header must compile standalone and link from the
# static library using no internal type.
add_executable(abi-header-hygiene abi_header_hygiene.c)
target_include_directories(
    abi-header-hygiene
    PRIVATE
        "${CMAKE_SOURCE_DIR}/include"
)
target_link_libraries(abi-header-hygiene PRIVATE wsh_static)

# The identical cases run against the shared library to prove the static and
# shared embedding surfaces are behaviorally equivalent.
add_executable(abi-conformance-shared-tests abi_conformance_tests.c)
target_include_directories(
    abi-conformance-shared-tests
    PRIVATE
        "${CMAKE_SOURCE_DIR}/include"
)
target_link_libraries(abi-conformance-shared-tests PRIVATE wsh_shared)

# The public C host example must build against the installed public headers
# only and link the static embedding library, proving no internal type leaks.
add_executable(embedding-host-example
    "${CMAKE_SOURCE_DIR}/examples/embedding/host.c")
target_include_directories(
    embedding-host-example
    PRIVATE
        "${CMAKE_SOURCE_DIR}/include"
)
target_link_libraries(embedding-host-example PRIVATE wsh_static)

# M9 input fuzzing smoke harness over the public decode/lex/parse surface.
add_executable(fuzz-smoke fuzz_smoke.c)
target_include_directories(
    fuzz-smoke
    PRIVATE
        "${CMAKE_SOURCE_DIR}/include"
)
target_link_libraries(fuzz-smoke PRIVATE wsh_static)

# M9 resource-exhaustion bound checks for the portable core.
add_executable(resource-bounds resource_bounds.c)
target_include_directories(
    resource-bounds
    PRIVATE
        "${CMAKE_SOURCE_DIR}/include"
)
target_link_libraries(resource-bounds PRIVATE wsh_static)

add_executable(
    frontend-tests
    frontend_tests.c
    "${CMAKE_SOURCE_DIR}/src/frontend.c"
)
target_include_directories(
    frontend-tests
    PRIVATE
        "${CMAKE_SOURCE_DIR}/include"
        "${CMAKE_SOURCE_DIR}/src"
)
target_link_libraries(frontend-tests PRIVATE wsh_core)
wsh_configure_executable(fuzz-smoke)
wsh_configure_executable(resource-bounds)
wsh_configure_executable(abi-conformance-tests)
wsh_configure_executable(abi-header-hygiene)
wsh_configure_executable(abi-conformance-shared-tests)
wsh_configure_executable(embedding-host-example)
wsh_configure_executable(portable-core-tests)
wsh_configure_executable(parser-tests)
wsh_configure_executable(evaluator-tests)
wsh_configure_executable(runtime-probe)
wsh_configure_executable(console-driver)
wsh_configure_executable(windows-runtime-tests)
wsh_configure_executable(frontend-tests)

set(WSH_TEST_TOOLCHAIN "TinyCC ${WSH_TINYCC_VERSION}")
set(WSH_TEST_TARGET_ARCHITECTURE "${WSH_TARGET_ARCHITECTURE}")
set(WSH_TEST_TIMEOUT 120)

add_test(NAME version-smoke COMMAND wsh --version)
string(REPLACE "." "\\." WSH_TEST_VERSION_PATTERN "${PROJECT_VERSION}")
set_tests_properties(
    version-smoke
    PROPERTIES
        PASS_REGULAR_EXPRESSION
            "Waughtal Shell.*Version ${WSH_TEST_VERSION_PATTERN}"
        TIMEOUT "${WSH_TEST_TIMEOUT}"
)

add_test(NAME print-abi-smoke COMMAND wsh --print-abi)
set_tests_properties(
    print-abi-smoke
    PROPERTIES
        PASS_REGULAR_EXPRESSION "wsh embedding ABI 1"
        TIMEOUT "${WSH_TEST_TIMEOUT}"
)

# The controlled M8 embedding cases (below, after the PowerShell discovery)
# execute these built artifacts. The optional Python interpreter drives the
# second-language FFI case; the runner skips it when the interpreter bitness
# does not match the target.
find_program(WSH_PYTHON_EXECUTABLE NAMES python)
if(NOT WSH_PYTHON_EXECUTABLE)
    set(WSH_PYTHON_EXECUTABLE "")
    message(STATUS "Python not found; the M8 FFI case will skip")
endif()

add_test(NAME fuzz-smoke COMMAND fuzz-smoke)
set_tests_properties(
    fuzz-smoke
    PROPERTIES
        LABELS "M9;security;fuzz"
        TIMEOUT "${WSH_TEST_TIMEOUT}"
)

add_test(NAME resource-bounds COMMAND resource-bounds)
set_tests_properties(
    resource-bounds
    PROPERTIES
        LABELS "M9;security;limits"
        TIMEOUT "${WSH_TEST_TIMEOUT}"
)

add_test(NAME frontend-unit COMMAND frontend-tests)
set_tests_properties(
    frontend-unit
    PROPERTIES
        LABELS "interactive;unit"
        TIMEOUT "${WSH_TEST_TIMEOUT}"
)

add_test(NAME evaluator-unit COMMAND evaluator-tests)
set_tests_properties(
    evaluator-unit
    PROPERTIES
        LABELS "M4;unit"
        TIMEOUT "${WSH_TEST_TIMEOUT}"
)

find_program(
    WSH_POWERSHELL_EXECUTABLE
    NAMES pwsh
    REQUIRED
)

add_test(
    NAME m8-traceability
    COMMAND
        "${WSH_POWERSHELL_EXECUTABLE}"
        -NoProfile
        -File "${CMAKE_SOURCE_DIR}/wsp/tools/Test-Traceability.ps1"
        -RepositoryRoot "${CMAKE_SOURCE_DIR}"
        -RequirementsPath "docs/requirements/m8"
        -TestSpecificationsPath "docs/tests/m8"
        -TestImplementationsPath "tests/m8"
)
set_tests_properties(m8-traceability PROPERTIES LABELS "M8;quality")

set(WSH_M8_TEST_IDS 0100 0101 0102 0103 0104 0105)
set(WSH_M8_CTEST_NAMES)
foreach(test_id IN LISTS WSH_M8_TEST_IDS)
    add_test(
        NAME "m8-TC-${test_id}"
        COMMAND
            "${WSH_POWERSHELL_EXECUTABLE}"
            -NoProfile
            -File "${CMAKE_CURRENT_SOURCE_DIR}/m8/run-tc-${test_id}-m8.ps1"
            -ConformanceStatic "$<TARGET_FILE:abi-conformance-tests>"
            -ConformanceShared "$<TARGET_FILE:abi-conformance-shared-tests>"
            -HostExample "$<TARGET_FILE:embedding-host-example>"
            -HeaderHygiene "$<TARGET_FILE:abi-header-hygiene>"
            -SharedLibrary "$<TARGET_FILE:wsh_shared>"
            -Manifest "${CMAKE_CURRENT_SOURCE_DIR}/embedding/abi1-exports.txt"
            -VerifyExports "${CMAKE_CURRENT_SOURCE_DIR}/verify-abi-exports.ps1"
            -VerifyInstalled "${CMAKE_CURRENT_SOURCE_DIR}/verify-installed-sdk.ps1"
            -Python "${WSH_PYTHON_EXECUTABLE}"
            -PythonScript "${CMAKE_SOURCE_DIR}/examples/embedding/host.py"
            -Compiler "${CMAKE_C_COMPILER}"
            -BuildDir "${CMAKE_BINARY_DIR}"
            -Config "$<CONFIG>"
            -HostSource "${CMAKE_SOURCE_DIR}/examples/embedding/host.c"
            -StageDir "${CMAKE_BINARY_DIR}/sdk-stage/$<CONFIG>"
            -TargetArchitecture "${WSH_TARGET_ARCHITECTURE}"
            -EvidenceDirectory "${CMAKE_BINARY_DIR}/test-evidence/m8/$<CONFIG>/current"
            -RepositoryRoot "${CMAKE_SOURCE_DIR}"
            -Toolchain "${WSH_TEST_TOOLCHAIN}"
    )
    set_tests_properties(
        "m8-TC-${test_id}"
        PROPERTIES
            DEPENDS m8-traceability
            LABELS "M8;controlled;embedding"
            TIMEOUT "${WSH_TEST_TIMEOUT}"
    )
    list(APPEND WSH_M8_CTEST_NAMES "m8-TC-${test_id}")
endforeach()

add_test(
    NAME m8-evidence
    COMMAND
        "${WSH_POWERSHELL_EXECUTABLE}"
        -NoProfile
        -File "${CMAKE_SOURCE_DIR}/wsp/tools/Test-TestEvidence.ps1"
        -EvidenceDirectory "${CMAKE_BINARY_DIR}/test-evidence/m8/$<CONFIG>/current"
        -ExpectedCount 6
)
set_tests_properties(
    m8-evidence
    PROPERTIES
        DEPENDS "${WSH_M8_CTEST_NAMES}"
        LABELS "M8;quality"
)

add_test(
    NAME frontend-process
    COMMAND
        "${WSH_POWERSHELL_EXECUTABLE}"
        -NoProfile
        -File "${CMAKE_CURRENT_SOURCE_DIR}/run-frontend-process-tests.ps1"
        -Executable "$<TARGET_FILE:wsh>"
)
set_tests_properties(
    frontend-process
    PROPERTIES
        LABELS "interactive;integration"
        TIMEOUT "${WSH_TEST_TIMEOUT}"
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
            TIMEOUT "${WSH_TEST_TIMEOUT}"
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
            TIMEOUT "${WSH_TEST_TIMEOUT}"
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

set(WSH_M4_TEST_IDS
    0007
    0009
    0014
    0017
    0019
    0023
    0024
    0037
    0043
    0046
    0048
    0049
    0052
    0074
    0075
)

add_test(
    NAME m4-traceability
    COMMAND
        "${WSH_POWERSHELL_EXECUTABLE}"
        -NoProfile
        -File "${CMAKE_SOURCE_DIR}/wsp/tools/Test-Traceability.ps1"
        -RepositoryRoot "${CMAKE_SOURCE_DIR}"
        -RequirementsPath "docs/requirements/m4"
        -TestSpecificationsPath "docs/tests/m4"
        -TestImplementationsPath "tests/m4"
)
set_tests_properties(m4-traceability PROPERTIES LABELS "M4;quality")

if(WSH_ENABLE_SOURCE_QUALITY_TESTS)
    add_test(
        NAME m4-source-quality
        COMMAND
            "${WSH_POWERSHELL_EXECUTABLE}"
            -NoProfile
            -File "${CMAKE_CURRENT_SOURCE_DIR}/run-m4-source-quality.ps1"
            -RepositoryRoot "${CMAKE_SOURCE_DIR}"
            -Doxygen "${WSH_DOXYGEN_EXECUTABLE}"
    )
    set_tests_properties(
        m4-source-quality
        PROPERTIES
            DEPENDS m4-traceability
            LABELS "M4;quality"
    )
endif()

set(WSH_M4_CTEST_NAMES)
foreach(test_id IN LISTS WSH_M4_TEST_IDS)
    set(test_name "m4-TC-${test_id}")
    list(APPEND WSH_M4_CTEST_NAMES "${test_name}")
    add_test(
        NAME "${test_name}"
        COMMAND
            "${WSH_POWERSHELL_EXECUTABLE}"
            -NoProfile
            -File
                "${CMAKE_CURRENT_SOURCE_DIR}/m4/run-tc-${test_id}-m4.ps1"
            -TestCase "TC-${test_id}"
            -Executable "$<TARGET_FILE:evaluator-tests>"
            -EvidenceDirectory
                "${CMAKE_BINARY_DIR}/test-evidence/m4/$<CONFIG>/current"
            -RepositoryRoot "${CMAKE_SOURCE_DIR}"
            -Configuration "$<CONFIG>"
            -Toolchain "${WSH_TEST_TOOLCHAIN}"
            -TargetArchitecture "${WSH_TEST_TARGET_ARCHITECTURE}"
    )
    set_tests_properties(
        "${test_name}"
        PROPERTIES
            DEPENDS m4-traceability
            LABELS "M4;controlled"
            TIMEOUT "${WSH_TEST_TIMEOUT}"
    )
endforeach()

add_test(
    NAME m4-evidence
    COMMAND
        "${WSH_POWERSHELL_EXECUTABLE}"
        -NoProfile
        -File "${CMAKE_SOURCE_DIR}/wsp/tools/Test-TestEvidence.ps1"
        -EvidenceDirectory
            "${CMAKE_BINARY_DIR}/test-evidence/m4/$<CONFIG>/current"
        -ExpectedCount 15
)
set_tests_properties(
    m4-evidence
    PROPERTIES
        DEPENDS "${WSH_M4_CTEST_NAMES}"
        LABELS "M4;quality"
)

set(WSH_M5_TEST_IDS
    0011
    0012
    0013
    0015
    0016
    0018
    0024
    0039
    0042
    0043
    0045
    0047
    0050
    0051
    0052
    0070
    0074
    0075
)

add_test(
    NAME m5-traceability
    COMMAND
        "${WSH_POWERSHELL_EXECUTABLE}"
        -NoProfile
        -File "${CMAKE_SOURCE_DIR}/wsp/tools/Test-Traceability.ps1"
        -RepositoryRoot "${CMAKE_SOURCE_DIR}"
        -RequirementsPath "docs/requirements/m5"
        -TestSpecificationsPath "docs/tests/m5"
        -TestImplementationsPath "tests/m5"
)
set_tests_properties(m5-traceability PROPERTIES LABELS "M5;quality")

add_test(
    NAME m5-static-imports
    COMMAND
        "${WSH_POWERSHELL_EXECUTABLE}"
        -NoProfile
        -File "${CMAKE_CURRENT_SOURCE_DIR}/test-m5-static-imports.ps1"
        -Executable "$<TARGET_FILE:wsh>"
)
set_tests_properties(
    m5-static-imports
    PROPERTIES
        DEPENDS m5-traceability
        LABELS "M5;quality;security"
)

if(WSH_ENABLE_SOURCE_QUALITY_TESTS)
    add_test(
        NAME m5-source-quality
        COMMAND
            "${WSH_POWERSHELL_EXECUTABLE}"
            -NoProfile
            -File "${CMAKE_CURRENT_SOURCE_DIR}/run-m5-source-quality.ps1"
            -RepositoryRoot "${CMAKE_SOURCE_DIR}"
            -Doxygen "${WSH_DOXYGEN_EXECUTABLE}"
    )
    set_tests_properties(
        m5-source-quality
        PROPERTIES
            DEPENDS "m5-traceability;m5-static-imports"
            LABELS "M5;quality"
    )
endif()

set(WSH_M5_CTEST_NAMES)
foreach(test_id IN LISTS WSH_M5_TEST_IDS)
    set(test_name "m5-TC-${test_id}")
    list(APPEND WSH_M5_CTEST_NAMES "${test_name}")
    add_test(
        NAME "${test_name}"
        COMMAND
            "${WSH_POWERSHELL_EXECUTABLE}"
            -NoProfile
            -File
                "${CMAKE_CURRENT_SOURCE_DIR}/m5/run-tc-${test_id}-m5.ps1"
            -TestCase "TC-${test_id}"
            -Executable "$<TARGET_FILE:windows-runtime-tests>"
            -Probe "$<TARGET_FILE:runtime-probe>"
            -Wsh "$<TARGET_FILE:wsh>"
            -EvidenceDirectory
                "${CMAKE_BINARY_DIR}/test-evidence/m5/$<CONFIG>/current"
            -RepositoryRoot "${CMAKE_SOURCE_DIR}"
            -Configuration "$<CONFIG>"
            -Toolchain "${WSH_TEST_TOOLCHAIN}"
            -TargetArchitecture "${WSH_TEST_TARGET_ARCHITECTURE}"
    )
    set_tests_properties(
        "${test_name}"
        PROPERTIES
            DEPENDS m5-traceability
            LABELS "M5;controlled"
            TIMEOUT "${WSH_TEST_TIMEOUT}"
    )
endforeach()

add_test(
    NAME m5-evidence
    COMMAND
        "${WSH_POWERSHELL_EXECUTABLE}"
        -NoProfile
        -File "${CMAKE_SOURCE_DIR}/wsp/tools/Test-TestEvidence.ps1"
        -EvidenceDirectory
            "${CMAKE_BINARY_DIR}/test-evidence/m5/$<CONFIG>/current"
        -ExpectedCount 18
)
set_tests_properties(
    m5-evidence
    PROPERTIES
        DEPENDS "${WSH_M5_CTEST_NAMES}"
        LABELS "M5;quality"
)

set(WSH_M6_TEST_IDS
    0024
    0037
    0065
    0066
    0067
    0068
    0070
    0074
    0075
)

add_test(
    NAME m6-traceability
    COMMAND
        "${WSH_POWERSHELL_EXECUTABLE}"
        -NoProfile
        -File "${CMAKE_SOURCE_DIR}/wsp/tools/Test-Traceability.ps1"
        -RepositoryRoot "${CMAKE_SOURCE_DIR}"
        -RequirementsPath "docs/requirements/m6"
        -TestSpecificationsPath "docs/tests/m6"
        -TestImplementationsPath "tests/m6"
)
set_tests_properties(m6-traceability PROPERTIES LABELS "M6;quality")

if(WSH_ENABLE_SOURCE_QUALITY_TESTS)
    add_test(
        NAME m6-source-quality
        COMMAND
            "${WSH_POWERSHELL_EXECUTABLE}"
            -NoProfile
            -File "${CMAKE_CURRENT_SOURCE_DIR}/run-m6-source-quality.ps1"
            -RepositoryRoot "${CMAKE_SOURCE_DIR}"
            -Doxygen "${WSH_DOXYGEN_EXECUTABLE}"
    )
    set_tests_properties(
        m6-source-quality
        PROPERTIES
            DEPENDS m6-traceability
            LABELS "M6;quality"
    )
endif()

set(WSH_M6_CTEST_NAMES)
foreach(test_id IN LISTS WSH_M6_TEST_IDS)
    set(test_name "m6-TC-${test_id}")
    list(APPEND WSH_M6_CTEST_NAMES "${test_name}")
    add_test(
        NAME "${test_name}"
        COMMAND
            "${WSH_POWERSHELL_EXECUTABLE}"
            -NoProfile
            -File
                "${CMAKE_CURRENT_SOURCE_DIR}/m6/run-tc-${test_id}-m6.ps1"
            -TestCase "TC-${test_id}"
            -Wsh "$<TARGET_FILE:wsh>"
            -Probe "$<TARGET_FILE:runtime-probe>"
            -EvidenceDirectory
                "${CMAKE_BINARY_DIR}/test-evidence/m6/$<CONFIG>/current"
            -RepositoryRoot "${CMAKE_SOURCE_DIR}"
            -Configuration "$<CONFIG>"
            -Toolchain "${WSH_TEST_TOOLCHAIN}"
            -TargetArchitecture "${WSH_TEST_TARGET_ARCHITECTURE}"
    )
    set_tests_properties(
        "${test_name}"
        PROPERTIES
            DEPENDS m6-traceability
            LABELS "M6;controlled"
            TIMEOUT "${WSH_TEST_TIMEOUT}"
    )
endforeach()

add_test(
    NAME m6-self-use
    COMMAND
        wsh
        "${CMAKE_SOURCE_DIR}/examples/build-and-test.wsh"
        "$<LOWER_CASE:${WSH_TARGET_ARCHITECTURE}-$<CONFIG>>"
        "$<CONFIG>"
)
set_tests_properties(
    m6-self-use
    PROPERTIES
        DEPENDS "${WSH_M6_CTEST_NAMES}"
        LABELS "M6;self-use;integration"
        TIMEOUT 300
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
)

add_test(
    NAME m6-evidence
    COMMAND
        "${WSH_POWERSHELL_EXECUTABLE}"
        -NoProfile
        -File "${CMAKE_SOURCE_DIR}/wsp/tools/Test-TestEvidence.ps1"
        -EvidenceDirectory
            "${CMAKE_BINARY_DIR}/test-evidence/m6/$<CONFIG>/current"
        -ExpectedCount 9
)
set_tests_properties(
    m6-evidence
    PROPERTIES
        DEPENDS "${WSH_M6_CTEST_NAMES}"
        LABELS "M6;quality"
)

set(WSH_M7_TEST_IDS
    0020
    0053
    0054
    0055
    0056
    0057
    0074
    0075
)

add_test(
    NAME m7-traceability
    COMMAND
        "${WSH_POWERSHELL_EXECUTABLE}"
        -NoProfile
        -File "${CMAKE_SOURCE_DIR}/wsp/tools/Test-Traceability.ps1"
        -RepositoryRoot "${CMAKE_SOURCE_DIR}"
        -RequirementsPath "docs/requirements/m7"
        -TestSpecificationsPath "docs/tests/m7"
        -TestImplementationsPath "tests/m7"
)
set_tests_properties(m7-traceability PROPERTIES LABELS "M7;quality")
set(WSH_M7_QUALITY_DEPENDS m7-traceability)

if(WSH_ENABLE_SOURCE_QUALITY_TESTS)
    add_test(
        NAME m7-source-quality
        COMMAND
            "${WSH_POWERSHELL_EXECUTABLE}"
            -NoProfile
            -File "${CMAKE_CURRENT_SOURCE_DIR}/run-m7-source-quality.ps1"
            -RepositoryRoot "${CMAKE_SOURCE_DIR}"
            -Doxygen "${WSH_DOXYGEN_EXECUTABLE}"
    )
    set_tests_properties(
        m7-source-quality
        PROPERTIES
            DEPENDS m7-traceability
            LABELS "M7;quality"
    )
    list(APPEND WSH_M7_QUALITY_DEPENDS m7-source-quality)
endif()

set(WSH_M7_CTEST_NAMES)
foreach(test_id IN LISTS WSH_M7_TEST_IDS)
    if(test_id STREQUAL "0020" OR test_id STREQUAL "0056")
        set(test_scenario "control")
    elseif(test_id STREQUAL "0053")
        set(test_scenario "editor")
    elseif(test_id STREQUAL "0054")
        set(test_scenario "history")
    elseif(test_id STREQUAL "0055")
        set(test_scenario "completion")
    elseif(test_id STREQUAL "0057")
        set(test_scenario "recovery")
    elseif(test_id STREQUAL "0074")
        set(test_scenario "limits")
    else()
        set(test_scenario "security")
    endif()
    set(test_name "m7-TC-${test_id}")
    list(APPEND WSH_M7_CTEST_NAMES "${test_name}")
    add_test(
        NAME "${test_name}"
        COMMAND
            "${WSH_POWERSHELL_EXECUTABLE}"
            -NoProfile
            -File
                "${CMAKE_CURRENT_SOURCE_DIR}/m7/run-tc-${test_id}-m7.ps1"
            -TestCase "TC-${test_id}"
            -Driver "$<TARGET_FILE:console-driver>"
            -Wsh "$<TARGET_FILE:wsh>"
            -Probe "$<TARGET_FILE:runtime-probe>"
            -Scenario "${test_scenario}"
            -EvidenceDirectory
                "${CMAKE_BINARY_DIR}/test-evidence/m7/$<CONFIG>/current"
            -RepositoryRoot "${CMAKE_SOURCE_DIR}"
            -Configuration "$<CONFIG>"
            -Toolchain "${WSH_TEST_TOOLCHAIN}"
            -TargetArchitecture "${WSH_TEST_TARGET_ARCHITECTURE}"
    )
    set_tests_properties(
        "${test_name}"
        PROPERTIES
            DEPENDS "${WSH_M7_QUALITY_DEPENDS}"
            LABELS "M7;controlled;interactive;native-console"
            TIMEOUT 180
    )
endforeach()

add_test(
    NAME m7-evidence
    COMMAND
        "${WSH_POWERSHELL_EXECUTABLE}"
        -NoProfile
        -File "${CMAKE_SOURCE_DIR}/wsp/tools/Test-TestEvidence.ps1"
        -EvidenceDirectory
            "${CMAKE_BINARY_DIR}/test-evidence/m7/$<CONFIG>/current"
        -ExpectedCount 8
)
set_tests_properties(
    m7-evidence
    PROPERTIES
        DEPENDS "${WSH_M7_CTEST_NAMES}"
        LABELS "M7;quality"
)
