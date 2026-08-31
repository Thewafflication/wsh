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


class StringView(ctypes.Structure):
    """Length-delimited borrowed UTF-8 view from the public ABI."""

    _fields_ = [("data", ctypes.c_void_p), ("length", ctypes.c_size_t)]


def view(value: bytes) -> tuple[ctypes.Array, StringView]:
    """Keep a byte buffer alive beside its ABI view."""

    buffer = ctypes.create_string_buffer(value)
    return buffer, StringView(ctypes.cast(buffer, ctypes.c_void_p), len(value))


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
    library.wsh_evaluator_create.restype = ctypes.c_int
    library.wsh_evaluator_create.argtypes = [
        ctypes.c_void_p,
        ctypes.c_void_p,
        ctypes.POINTER(ctypes.c_void_p),
    ]
    library.wsh_evaluator_destroy.restype = None
    library.wsh_evaluator_destroy.argtypes = [ctypes.c_void_p]
    host_callback_type = ctypes.CFUNCTYPE(
        ctypes.c_int,
        ctypes.c_void_p,
        ctypes.c_void_p,
        ctypes.c_void_p,
        ctypes.c_void_p,
        ctypes.c_void_p,
    )
    library.wsh_evaluator_register_host_command.restype = ctypes.c_int
    library.wsh_evaluator_register_host_command.argtypes = [
        ctypes.c_void_p,
        StringView,
        host_callback_type,
        ctypes.c_void_p,
    ]
    library.wsh_source_create.restype = ctypes.c_int
    library.wsh_source_create.argtypes = [
        ctypes.c_void_p,
        ctypes.c_void_p,
        ctypes.c_void_p,
        ctypes.c_size_t,
        ctypes.POINTER(ctypes.c_void_p),
    ]
    library.wsh_source_destroy.restype = None
    library.wsh_source_destroy.argtypes = [ctypes.c_void_p]
    library.wsh_parse.restype = ctypes.c_int
    library.wsh_parse.argtypes = [
        ctypes.c_void_p,
        ctypes.c_void_p,
        ctypes.POINTER(ctypes.c_void_p),
    ]
    library.wsh_parse_tree_destroy.restype = None
    library.wsh_parse_tree_destroy.argtypes = [ctypes.c_void_p]
    library.wsh_evaluate.restype = ctypes.c_int
    library.wsh_evaluate.argtypes = [
        ctypes.c_void_p,
        ctypes.c_void_p,
        ctypes.POINTER(ctypes.c_void_p),
    ]
    library.wsh_status_builder_append.restype = ctypes.c_int
    library.wsh_status_builder_append.argtypes = [ctypes.c_void_p, ctypes.c_uint32]
    library.wsh_status_list_count.restype = ctypes.c_size_t
    library.wsh_status_list_count.argtypes = [ctypes.c_void_p]
    library.wsh_status_list_destroy.restype = None
    library.wsh_status_list_destroy.argtypes = [ctypes.c_void_p]
    library.wsh_value_count.restype = ctypes.c_size_t
    library.wsh_value_count.argtypes = [ctypes.c_void_p]

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

    evaluator = ctypes.c_void_p()
    if library.wsh_evaluator_create(context, None, ctypes.byref(evaluator)) != 0:
        sys.stderr.write("host.py: evaluator create failed\n")
        library.wsh_context_destroy(context)
        return 1

    calls = 0

    @host_callback_type
    def python_command(_state, callback_context, arguments, _output, status):
        nonlocal calls
        if callback_context != context.value or library.wsh_value_count(arguments) != 1:
            return 1
        calls += 1
        return library.wsh_status_builder_append(status, 0)

    # Keep the borrowed name buffer alive until evaluation completes.
    name_bytes, name = view(b"host::python")
    if library.wsh_evaluator_register_host_command(
            evaluator, name, python_command, None) != 0:
        sys.stderr.write("host.py: host command registration failed\n")
        library.wsh_evaluator_destroy(evaluator)
        library.wsh_context_destroy(context)
        return 1

    source_bytes = ctypes.create_string_buffer(b"host::python structured")
    source = ctypes.c_void_p()
    tree = ctypes.c_void_p()
    status = ctypes.c_void_p()
    result = library.wsh_source_create(
        None,
        None,
        ctypes.cast(source_bytes, ctypes.c_void_p),
        len(b"host::python structured"),
        ctypes.byref(source),
    )
    if result == 0:
        result = library.wsh_parse(None, source, ctypes.byref(tree))
    if result == 0:
        result = library.wsh_evaluate(evaluator, tree, ctypes.byref(status))
    if result != 0 or calls != 1 or library.wsh_status_list_count(status) != 1:
        sys.stderr.write("host.py: host command invocation failed\n")
        library.wsh_status_list_destroy(status)
        library.wsh_parse_tree_destroy(tree)
        library.wsh_source_destroy(source)
        library.wsh_evaluator_destroy(evaluator)
        library.wsh_context_destroy(context)
        return 1
    library.wsh_status_list_destroy(status)
    library.wsh_parse_tree_destroy(tree)
    library.wsh_source_destroy(source)
    library.wsh_evaluator_destroy(evaluator)

    library.wsh_context_destroy(context)
    print("host.py: ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())
