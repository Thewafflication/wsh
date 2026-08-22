cmake_minimum_required(VERSION 3.20)

project(wsh VERSION 1.1.0 LANGUAGES C)

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

add_library(wsh_core STATIC
    src/parser.c
    src/portable_core.c
)
target_include_directories(wsh_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(wsh_core PUBLIC WCRT::WCRT)

add_library(wsh_static STATIC src/wshlib.c)
add_library(wsh_shared SHARED src/wshlib.c)

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
target_link_options(wsh_shared PRIVATE "-Wl,-export-all-symbols")

target_include_directories(wsh_static
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
)
target_include_directories(wsh_shared
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

target_link_libraries(wsh_static PUBLIC wsh_core)
target_link_libraries(wsh_shared PUBLIC wsh_core)

target_compile_definitions(wsh_static PRIVATE
    WSH_VERSION="${PROJECT_VERSION}"
    WSH_WCRT_VERSION="${WSH_WCRT_VERSION}")
target_compile_definitions(wsh_shared PRIVATE
    WSH_VERSION="${PROJECT_VERSION}"
    WSH_WCRT_VERSION="${WSH_WCRT_VERSION}")

add_executable(wsh
    src/frontend.c
    src/main.c
)

target_include_directories(wsh
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

target_link_libraries(wsh PRIVATE wsh_static)
wsh_configure_executable(wsh)

install(TARGETS wsh wsh_static wsh_shared
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    PUBLIC_HEADER DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/wsh
)

install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/include/" DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

add_subdirectory(tests)
