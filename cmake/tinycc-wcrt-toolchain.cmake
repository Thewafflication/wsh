# TinyCC/WCRT toolchain for the checked-in x86, x64, and ARM64 presets.

set(CMAKE_SYSTEM_NAME Windows)

set(WSH_TARGET_ARCHITECTURE "x64" CACHE STRING
    "WSH target architecture: x86, x64, or arm64")
set_property(CACHE WSH_TARGET_ARCHITECTURE PROPERTY STRINGS x86 x64 arm64)

if(NOT WSH_TARGET_ARCHITECTURE MATCHES "^(x86|x64|arm64)$")
    message(FATAL_ERROR
        "WSH_TARGET_ARCHITECTURE must be x86, x64, or arm64")
endif()

set(_wsh_tinycc_default
    "$ENV{ProgramFiles}/TinyCC/0.9.28-rc.1444+9a4be30f")
if(NOT IS_DIRECTORY "${_wsh_tinycc_default}")
    set(_wsh_tinycc_default "$ENV{TCC_HOME}")
endif()
set(_wsh_wcrt_default "$ENV{ProgramFiles}/WCRT/1.0.0")
if(NOT IS_DIRECTORY "${_wsh_wcrt_default}")
    set(_wsh_wcrt_default "$ENV{WCRT_HOME}")
endif()

set(WSH_TINYCC_ROOT "${_wsh_tinycc_default}" CACHE PATH
    "Root of the WPM-installed TinyCC package")
set(WSH_WCRT_ROOT "${_wsh_wcrt_default}" CACHE PATH
    "Root of the WPM-installed WCRT package")

if(NOT IS_DIRECTORY "${WSH_TINYCC_ROOT}")
    message(FATAL_ERROR
        "TinyCC was not found. Install it with WPM or set TCC_HOME.")
endif()
if(NOT IS_DIRECTORY "${WSH_WCRT_ROOT}")
    message(FATAL_ERROR
        "WCRT was not found. Install it with WPM or set WCRT_HOME.")
endif()

if(WSH_TARGET_ARCHITECTURE STREQUAL "x86")
    set(_wsh_tinycc_prefix "i386-win32")
    set(CMAKE_SYSTEM_PROCESSOR "x86")
elseif(WSH_TARGET_ARCHITECTURE STREQUAL "x64")
    set(_wsh_tinycc_prefix "x86_64-win32")
    set(CMAKE_SYSTEM_PROCESSOR "AMD64")
else()
    set(_wsh_tinycc_prefix "arm64-win32")
    set(CMAKE_SYSTEM_PROCESSOR "ARM64")
endif()

set(_wsh_tinycc
    "${WSH_TINYCC_ROOT}/${_wsh_tinycc_prefix}-tcc.exe")
if(NOT EXISTS "${_wsh_tinycc}")
    message(FATAL_ERROR
        "The TinyCC compiler was not found: ${_wsh_tinycc}")
endif()

set(_wsh_wcrt_library
    "${WSH_WCRT_ROOT}/${WSH_TARGET_ARCHITECTURE}/lib/libwcrt.a")
set(_wsh_wcrt_console_startup
    "${WSH_WCRT_ROOT}/${WSH_TARGET_ARCHITECTURE}/lib/wcrt-startup-console.o")
set(_wsh_tinycc_support
    "${WSH_TINYCC_ROOT}/lib/${_wsh_tinycc_prefix}-libtcc1.a")
set(_wsh_tinycc_kernel_definition
    "${WSH_TINYCC_ROOT}/lib/kernel32.def")
if(NOT EXISTS "${WSH_WCRT_ROOT}/include/stdio.h" OR
   NOT EXISTS "${_wsh_wcrt_library}" OR
   NOT EXISTS "${_wsh_wcrt_console_startup}")
    message(FATAL_ERROR
        "The WCRT ${WSH_TARGET_ARCHITECTURE} development files are incomplete")
endif()
if(NOT EXISTS "${_wsh_tinycc_support}" OR
   NOT EXISTS "${_wsh_tinycc_kernel_definition}")
    message(FATAL_ERROR
        "The TinyCC ${WSH_TARGET_ARCHITECTURE} link files are incomplete")
endif()

set(CMAKE_C_COMPILER "${_wsh_tinycc}" CACHE FILEPATH
    "TinyCC C compiler" FORCE)

# CMake recognizes TinyCC's compiler driver, but it does not currently ship a
# TinyCC archiver rule. TinyCC's -ar mode creates the static archives directly.
set(CMAKE_C_CREATE_STATIC_LIBRARY
    "<CMAKE_C_COMPILER> -ar rcs <TARGET> <OBJECTS>")
set(CMAKE_SHARED_LIBRARY_C_FLAGS "-shared")

set(CMAKE_C_FLAGS_DEBUG "-gdwarf" CACHE STRING
    "TinyCC Debug flags" FORCE)
set(CMAKE_C_FLAGS_RELEASE "-O2 -DNDEBUG" CACHE STRING
    "TinyCC Release flags" FORCE)

get_filename_component(WSH_TINYCC_VERSION "${WSH_TINYCC_ROOT}" NAME)
get_filename_component(WSH_WCRT_VERSION "${WSH_WCRT_ROOT}" NAME)
set(WSH_TINYCC_VERSION "${WSH_TINYCC_VERSION}" CACHE STRING
    "Selected TinyCC package version" FORCE)
set(WSH_WCRT_VERSION "${WSH_WCRT_VERSION}" CACHE STRING
    "Selected WCRT package version" FORCE)
set(WSH_WCRT_INCLUDE_DIR "${WSH_WCRT_ROOT}/include" CACHE PATH
    "Selected WCRT include directory" FORCE)
set(WSH_WCRT_LIBRARY "${_wsh_wcrt_library}" CACHE FILEPATH
    "Selected WCRT static library" FORCE)
set(WSH_WCRT_CONSOLE_STARTUP "${_wsh_wcrt_console_startup}" CACHE FILEPATH
    "Selected WCRT console startup object" FORCE)
set(WSH_TINYCC_SUPPORT_LIBRARY "${_wsh_tinycc_support}" CACHE FILEPATH
    "Selected TinyCC compiler-support archive" FORCE)
set(WSH_TINYCC_KERNEL_DEFINITION "${_wsh_tinycc_kernel_definition}"
    CACHE FILEPATH "Selected TinyCC kernel32 import definition" FORCE)

unset(_wsh_tinycc)
unset(_wsh_tinycc_default)
unset(_wsh_tinycc_prefix)
unset(_wsh_tinycc_kernel_definition)
unset(_wsh_tinycc_support)
unset(_wsh_wcrt_console_startup)
unset(_wsh_wcrt_default)
unset(_wsh_wcrt_library)
