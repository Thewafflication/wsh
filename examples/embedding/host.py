#!/usr/bin/env python3
"""Second-language FFI host that drives the WSH embedding ABI via ctypes.

This host loads the shared embedding library and calls the stable C ABI from
Python, demonstrating that a non-C language can create an isolated context,
inspect it, and release it across the boundary. It uses only exported ABI
symbols and no internal type. Struct-by-value entry points are covered by the
C conformance test; this host exercises opaque handles, output pointers, and
the default-allocator path, which are the FFI-critical shapes.

Usage: host.py <path-to-wsh-shared-library>
"""

import ctypes
import sys


def main() -> int:
    if len(sys.argv) != 2:
        sys.stderr.write("usage: host.py <shared-library>\n")
        return 2

    library = ctypes.CDLL(sys.argv[1])

    library.wsh_embedding_abi_version.restype = ctypes.c_uint
    library.wsh_embedding_abi_version.argtypes = []
    library.wsh_get_version_string.restype = ctypes.c_char_p
    library.wsh_get_version_string.argtypes = []
    library.wsh_get_runtime_name.restype = ctypes.c_char_p
    library.wsh_get_runtime_name.argtypes = []
    library.wsh_context_create.restype = ctypes.c_int
    library.wsh_context_create.argtypes = [
        ctypes.c_void_p,
        ctypes.POINTER(ctypes.c_void_p),
    ]
    library.wsh_context_variable_count.restype = ctypes.c_size_t
    library.wsh_context_variable_count.argtypes = [ctypes.c_void_p]
    library.wsh_context_diagnostic_count.restype = ctypes.c_size_t
    library.wsh_context_diagnostic_count.argtypes = [ctypes.c_void_p]
    library.wsh_context_destroy.restype = None
    library.wsh_context_destroy.argtypes = [ctypes.c_void_p]

    abi = library.wsh_embedding_abi_version()
    version = library.wsh_get_version_string()
    runtime = library.wsh_get_runtime_name()
    if abi != 1:
        sys.stderr.write("host.py: unexpected embedding ABI %d\n" % abi)
        return 1
    if not version:
        sys.stderr.write("host.py: empty version string\n")
        return 1
    if runtime != b"wcrt":
        sys.stderr.write("host.py: unexpected runtime name\n")
        return 1
    print("host.py: wsh version %s, runtime %s, ABI %d" % (
        version.decode("utf-8"),
        runtime.decode("utf-8"),
        abi,
    ))

    # WSH_OK is 0. A null options pointer selects the portable defaults.
    context = ctypes.c_void_p()
    result = library.wsh_context_create(None, ctypes.byref(context))
    if result != 0:
        sys.stderr.write("host.py: context create failed (%d)\n" % result)
        return 1

    variables = library.wsh_context_variable_count(context)
    diagnostics = library.wsh_context_diagnostic_count(context)
    print("host.py: fresh context has %d variables, %d diagnostics" % (
        variables,
        diagnostics,
    ))
    if variables != 0 or diagnostics != 0:
        sys.stderr.write("host.py: fresh context was not empty\n")
        library.wsh_context_destroy(context)
        return 1

    library.wsh_context_destroy(context)
    print("host.py: ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())
