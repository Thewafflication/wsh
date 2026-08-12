cmake_minimum_required(VERSION 3.20)

project(wsh VERSION 1.0.0 LANGUAGES C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

include(GNUInstallDirs)
include(CTest)

enable_testing()

set(WSH_EMBEDDING_ABI 1)

add_library(wsh_core STATIC src/portable_core.c)
target_include_directories(wsh_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")

if(MSVC)
    target_compile_options(wsh_core PRIVATE /W4 /WX)
else()
    target_compile_options(wsh_core PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

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

add_executable(wsh src/main.c)

target_include_directories(wsh
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

target_link_libraries(wsh PRIVATE wsh_static)

install(TARGETS wsh wsh_static wsh_shared
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    PUBLIC_HEADER DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/wsh
)

install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/include/" DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

add_subdirectory(tests)
