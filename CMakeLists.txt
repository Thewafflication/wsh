cmake_minimum_required(VERSION 3.20)

project(wsh VERSION 1.5.0 LANGUAGES C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

include(GNUInstallDirs)
include(CTest)

enable_testing()

set(WSH_EMBEDDING_ABI 1)

if(NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY
        "${CMAKE_BINARY_DIR}/${CMAKE_BUILD_TYPE}")
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY
        "${CMAKE_BINARY_DIR}/${CMAKE_BUILD_TYPE}")
    set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY
        "${CMAKE_BINARY_DIR}/${CMAKE_BUILD_TYPE}")
endif()

option(
    WSH_ENABLE_SOURCE_QUALITY_TESTS
    "Enable Doxygen-backed source-quality tests"
    ON
)

if(NOT CMAKE_C_COMPILER_ID STREQUAL "TinyCC")
    message(FATAL_ERROR "WSH requires the TinyCC/WCRT toolchain")
endif()

foreach(required_variable
    WSH_TARGET_ARCHITECTURE
    WSH_TINYCC_VERSION
    WSH_TINYCC_SUPPORT_LIBRARY
    WSH_TINYCC_KERNEL_DEFINITION
    WSH_WCRT_VERSION
    WSH_WCRT_CONSOLE_STARTUP
    WSH_WCRT_INCLUDE_DIR
    WSH_WCRT_LIBRARY)
    if(NOT DEFINED ${required_variable} OR
       "${${required_variable}}" STREQUAL "")
        message(FATAL_ERROR
            "${required_variable} must be provided by the "
            "TinyCC/WCRT toolchain file")
    endif()
endforeach()

add_library(WSH::TinyCCSupport STATIC IMPORTED GLOBAL)
set_target_properties(WSH::TinyCCSupport PROPERTIES
    IMPORTED_LOCATION "${WSH_TINYCC_SUPPORT_LIBRARY}"
)

add_library(WCRT::WCRT STATIC IMPORTED GLOBAL)
set_target_properties(WCRT::WCRT PROPERTIES
    IMPORTED_LOCATION "${WSH_WCRT_LIBRARY}"
    INTERFACE_INCLUDE_DIRECTORIES "${WSH_WCRT_INCLUDE_DIR}"
    INTERFACE_LINK_LIBRARIES
        "WSH::TinyCCSupport;${WSH_TINYCC_KERNEL_DEFINITION}"
)

function(wsh_configure_executable target)
    target_link_options(${target} PRIVATE
        -nostdlib
        "-Wl,-nostdlib"
        "-Wl,-subsystem=console"
        "${WSH_WCRT_CONSOLE_STARTUP}"
    )
endfunction()

add_compile_options(-std=c99 -Wall -Werror)

# Shared by the static core archive and the shared embedding library. The
# shared library must compile these objects directly so the whole embedding
# ABI is present in the DLL; linking the static archive would export only the
# objects referenced by wshlib.c and silently omit the rest of the surface.
set(WSH_CORE_SOURCES
    src/evaluator.c
    src/parser.c
    src/portable_core.c
    src/sha256.c
    src/standard_library.c
)

add_library(wsh_core STATIC ${WSH_CORE_SOURCES})
target_include_directories(wsh_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(wsh_core PUBLIC WCRT::WCRT)

add_library(wsh_windows_runtime STATIC
    src/platform/windows/windows_runtime.c
)
target_include_directories(
    wsh_windows_runtime
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
)
target_link_libraries(wsh_windows_runtime PUBLIC wsh_core)
target_compile_definitions(wsh_windows_runtime PRIVATE
    WSH_VERSION="${PROJECT_VERSION}"
    WSH_WCRT_VERSION="${WSH_WCRT_VERSION}")

add_library(wsh_static STATIC src/wshlib.c)
add_library(wsh_shared SHARED src/wshlib.c ${WSH_CORE_SOURCES})

set_target_properties(wsh_static PROPERTIES
    OUTPUT_NAME "wshlib"
    PUBLIC_HEADER "${CMAKE_CURRENT_SOURCE_DIR}/include/wsh/wsh.h"
)

set_target_properties(wsh_shared PROPERTIES
    OUTPUT_NAME "wshlib"
    VERSION ${PROJECT_VERSION}
    SOVERSION ${WSH_EMBEDDING_ABI}
    PUBLIC_HEADER "${CMAKE_CURRENT_SOURCE_DIR}/include/wsh/wsh.h"
)

# TinyCC writes an import definition beside the DLL. Give that output a
# distinct name so it cannot collide with the static wshlib archive.
set_target_properties(wsh_shared PROPERTIES
    IMPORT_PREFIX ""
    IMPORT_SUFFIX ".def"
)
# The shared library exports only the ABI 1 surface. Public declarations are
# marked WSH_API (__declspec(dllexport)) under WSH_SHARED_BUILD; with any such
# marker present TinyCC exports only the marked symbols, keeping runtime, CRT,
# and internal WSH functions out of the DLL. Export-all is deliberately absent.

target_include_directories(wsh_static
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
)
target_include_directories(wsh_shared
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

target_link_libraries(wsh_static PUBLIC wsh_core)
# The shared library compiles the core objects directly (see WSH_CORE_SOURCES),
# so it links only the runtime rather than the static core archive. This keeps
# the full ABI inside the DLL and lets consumers resolve it from the DLL import
# definition instead of a statically linked copy.
target_link_libraries(wsh_shared PUBLIC WCRT::WCRT)

target_compile_definitions(wsh_static PRIVATE
    WSH_VERSION="${PROJECT_VERSION}"
    WSH_WCRT_VERSION="${WSH_WCRT_VERSION}")
target_compile_definitions(wsh_shared PRIVATE
    WSH_SHARED_BUILD
    WSH_VERSION="${PROJECT_VERSION}"
    WSH_WCRT_VERSION="${WSH_WCRT_VERSION}")

add_executable(wsh
    src/frontend.c
    src/interactive.c
    src/main.c
)

target_include_directories(wsh
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

target_link_libraries(wsh PRIVATE wsh_static wsh_windows_runtime)
wsh_configure_executable(wsh)

install(TARGETS wsh wsh_static wsh_shared
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    PUBLIC_HEADER DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/wsh
)

install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/include/" DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

add_subdirectory(tests)
