/**
 * @file windows_runtime.h
 * @brief Concrete bounded Windows runtime for WSH process composition.
 */

#ifndef WSH_WINDOWS_RUNTIME_H
#define WSH_WINDOWS_RUNTIME_H

#include "wsh/core.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Opaque owner of Windows directories, handles, jobs, and children. */
typedef struct wsh_windows_runtime wsh_windows_runtime;

/** Configurable finite Windows runtime policy and resource ceilings. */
typedef struct wsh_windows_runtime_options {
    /** Allocator used for runtime-owned portable buffers. */
    wsh_allocator allocator;
    /** Core text and list limits copied by the runtime. */
    wsh_limits limits;
    /** Maximum simultaneously registered foreground/background processes. */
    size_t max_children;
    /** Maximum bytes returned by one command capture. */
    size_t max_capture_bytes;
    /** Default finite timeout in milliseconds, or zero for no timeout. */
    uint32_t default_timeout_milliseconds;
    /** Grace interval before forced cancellation. */
    uint32_t cancellation_grace_milliseconds;
    /** Nonzero removes implicit current-directory command search. */
    int safe_path;
    /** Nonzero permits explicit raw launch. */
    int allow_raw_launch;
    /** Nonzero forces the reviewed Windows 2000 inheritance path in tests. */
    int force_legacy_inheritance;
    /** Nonzero disables job assignment and exercises tracked fallback. */
    int force_tracked_fallback;
} wsh_windows_runtime_options;

/** Observable optional-API and containment capabilities. */
typedef struct wsh_windows_runtime_capabilities {
    /** Nonzero when a modern explicit process handle list is usable. */
    int explicit_handle_list;
    /** Nonzero when a job object was created for containment. */
    int job_object;
    /** Nonzero after nested-job restrictions selected tracked fallback. */
    int tracked_fallback;
} wsh_windows_runtime_capabilities;

/** Initialize conservative finite Windows runtime defaults. */
void wsh_windows_runtime_options_init(
    wsh_windows_runtime_options *out_options);

/**
 * Create one isolated Windows runtime.
 * @param options Options, or null for defaults.
 * @param out_runtime Receives the owned runtime.
 * @return WSH_OK or an argument, encoding, or resource error.
 */
wsh_result wsh_windows_runtime_create(
    const wsh_windows_runtime_options *options,
    wsh_windows_runtime **out_runtime);

/** Cancel, collect, and destroy an owned Windows runtime. */
void wsh_windows_runtime_destroy(wsh_windows_runtime *runtime);

/** Return nonzero when a test::begin record still requires test::end. */
int wsh_windows_runtime_has_open_test(
    const wsh_windows_runtime *runtime);

/** Request status-130 cancellation of the active foreground group. */
void wsh_windows_runtime_request_interrupt(
    wsh_windows_runtime *runtime);

/** Return the number of retained background groups. */
size_t wsh_windows_runtime_background_count(
    const wsh_windows_runtime *runtime);

/** Borrow one retained background group's root process identifier. */
wsh_result wsh_windows_runtime_background_at(
    const wsh_windows_runtime *runtime,
    size_t index,
    uint32_t *out_identifier);

/** Cancel and collect every retained background group. */
wsh_result wsh_windows_runtime_cancel_all(
    wsh_windows_runtime *runtime);

/** Return an owned UTF-8 copy of the logical working directory. */
wsh_result wsh_windows_runtime_working_directory(
    const wsh_windows_runtime *runtime,
    wsh_string **out_path);

/** Return the abstract-runtime callbacks implemented by runtime. */
wsh_runtime wsh_windows_runtime_interface(wsh_windows_runtime *runtime);

/**
 * Import the process environment into an otherwise isolated context.
 * @param runtime Runtime supplying comparison and conversion policy.
 * @param context Destination context.
 * @return WSH_OK or an encoding, collision, or resource error.
 */
wsh_result wsh_windows_runtime_import_environment(
    wsh_windows_runtime *runtime,
    wsh_context *context);

/** Return copied optional-API and containment capabilities. */
wsh_result wsh_windows_runtime_get_capabilities(
    const wsh_windows_runtime *runtime,
    wsh_windows_runtime_capabilities *out_capabilities);

/**
 * Resolve one external subject without launching it.
 * @param runtime Runtime with logical directory and safe-path policy.
 * @param context Context supplying the `path` value.
 * @param subject External name to resolve.
 * @param out_path Receives an owned exact UTF-8 resolved path.
 * @return WSH_OK, WSH_ERR_MISMATCH when absent, or another boundary error.
 */
wsh_result wsh_windows_runtime_resolve(
    wsh_windows_runtime *runtime,
    const wsh_context *context,
    wsh_string_view subject,
    wsh_string **out_path);

/**
 * Serialize one executable plus structured arguments to UTF-16.
 * @param runtime Runtime allocator and limits owner.
 * @param executable First argv element.
 * @param arguments Remaining structured arguments, or null.
 * @param out_units Receives an owned mutable zero-terminated command line.
 * @param out_length Receives UTF-16 units excluding the terminator.
 * @return WSH_OK or an encoding/resource error.
 */
wsh_result wsh_windows_runtime_serialize(
    const wsh_windows_runtime *runtime,
    wsh_string_view executable,
    const wsh_value *arguments,
    uint16_t **out_units,
    size_t *out_length);

/**
 * Build one explicit sorted Windows environment block for inspection/launch.
 * @param runtime Runtime allocator and limits owner.
 * @param context Context supplying exported variables.
 * @param nested_wsh Nonzero to include the bounded private WSH envelope.
 * @param out_units Receives an owned double-NUL-terminated UTF-16 block.
 * @param out_length Receives units including both final NUL units.
 * @return WSH_OK or a validation/resource error.
 */
wsh_result wsh_windows_runtime_environment_block(
    const wsh_windows_runtime *runtime,
    const wsh_context *context,
    int nested_wsh,
    uint16_t **out_units,
    size_t *out_length);

#ifdef __cplusplus
}
#endif

#endif
