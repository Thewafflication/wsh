/**
 * @file core.h
 * @brief Portable, side-effect-free WSH core contracts.
 *
 * All text accepted by this interface is length-delimited, strict UTF-8.
 * Owned objects retain the allocator that created them. Unless documented as
 * borrowed, an object returned through an output pointer belongs to the caller.
 */

#ifndef WSH_CORE_H
#define WSH_CORE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Result returned by portable-core operations. */
typedef enum wsh_result {
    /** The operation completed successfully. */
    WSH_OK = 0,
    /** An argument or object state violated the interface contract. */
    WSH_ERR_INVALID = 1,
    /** Input text was not valid WSH Unicode text. */
    WSH_ERR_ENCODING = 2,
    /** An allocator or configured resource limit rejected the operation. */
    WSH_ERR_RESOURCE = 3,
    /** An internal invariant was not satisfied. */
    WSH_ERR_INTERNAL = 4,
    /** A deterministic runtime expectation did not match. */
    WSH_ERR_MISMATCH = 5
} wsh_result;

/** A borrowed, length-delimited UTF-8 byte sequence. */
typedef struct wsh_string_view {
    /** First byte, or null only when length is zero. */
    const char *data;
    /** Number of bytes in data, excluding any convenience terminator. */
    size_t length;
} wsh_string_view;

/** Allocate a block for a WSH owner. */
typedef void *(*wsh_allocate_fn)(void *user_data, size_t size);

/** Release a block previously returned by the matching allocator. */
typedef void (*wsh_deallocate_fn)(void *user_data, void *pointer);

/** Allocator callbacks copied into each object they create. */
typedef struct wsh_allocator {
    /** Opaque value passed to both callbacks. */
    void *user_data;
    /** Required allocation callback. */
    wsh_allocate_fn allocate;
    /** Required deallocation callback. */
    wsh_deallocate_fn deallocate;
} wsh_allocator;

/** Resource ceilings applied before allocations or runtime calls. */
typedef struct wsh_limits {
    /** Maximum encoded or decoded source size in bytes. */
    size_t max_source_bytes;
    /** Maximum bytes in one immutable string. */
    size_t max_string_bytes;
    /** Maximum strings in one flat value. */
    size_t max_list_items;
    /** Maximum variables owned by one context. */
    size_t max_variables;
    /** Maximum diagnostics retained by one context. */
    size_t max_diagnostics;
    /** Maximum scripted expectations in a fake runtime. */
    size_t max_runtime_expectations;
    /** Maximum runtime calls made through one context. */
    size_t max_runtime_calls;
} wsh_limits;

/** A recognized source byte-order mark. */
typedef enum wsh_bom_kind {
    /** The input had no recognized leading BOM. */
    WSH_BOM_NONE = 0,
    /** The input began with a UTF-8 BOM. */
    WSH_BOM_UTF8 = 1,
    /** The input began with a little-endian UTF-16 BOM. */
    WSH_BOM_UTF16_LE = 2,
    /** The input began with a big-endian UTF-16 BOM. */
    WSH_BOM_UTF16_BE = 3
} wsh_bom_kind;

/** A position at a Unicode-scalar boundary in decoded source. */
typedef struct wsh_source_position {
    /** Offset in the original encoded source, including a leading BOM. */
    size_t original_byte_offset;
    /** Offset in normalized internal UTF-8. */
    size_t utf8_byte_offset;
    /** Zero-based Unicode-scalar offset. */
    size_t scalar_offset;
    /** One-based source line. */
    size_t line;
    /** One-based Unicode-scalar column. */
    size_t scalar_column;
    /** One-based display column with eight-column tabs. */
    size_t display_column;
} wsh_source_position;

/** A half-open source range. */
typedef struct wsh_source_span {
    /** Inclusive range start. */
    wsh_source_position start;
    /** Exclusive range end. */
    wsh_source_position end;
} wsh_source_span;

/** Opaque decoded and line-normalized source buffer. */
typedef struct wsh_source wsh_source;

/** Opaque immutable UTF-8 string. */
typedef struct wsh_string wsh_string;

/** Opaque fault-atomic immutable-string builder. */
typedef struct wsh_string_builder wsh_string_builder;

/** Opaque immutable ordered flat list of strings. */
typedef struct wsh_value wsh_value;

/** Opaque fault-atomic flat-list builder. */
typedef struct wsh_value_builder wsh_value_builder;

/** Opaque immutable list of unsigned Windows statuses. */
typedef struct wsh_status_list wsh_status_list;

/** Opaque fault-atomic status-list builder. */
typedef struct wsh_status_builder wsh_status_builder;

/** Opaque isolated portable-core context. */
typedef struct wsh_context wsh_context;

/** One ordered logical-descriptor action for a launch or write. */
typedef enum wsh_runtime_redirection_kind {
    /** Open an existing file for byte input. */
    WSH_RUNTIME_REDIRECT_INPUT = 1,
    /** Create or truncate a file for byte output. */
    WSH_RUNTIME_REDIRECT_OUTPUT = 2,
    /** Open or create a file and append byte output. */
    WSH_RUNTIME_REDIRECT_APPEND = 3,
    /** Supply exact here-document text through a pipe. */
    WSH_RUNTIME_REDIRECT_HERE = 4,
    /** Duplicate one current logical descriptor mapping. */
    WSH_RUNTIME_REDIRECT_DUPLICATE = 5,
    /** Close one current logical descriptor mapping. */
    WSH_RUNTIME_REDIRECT_CLOSE = 6
} wsh_runtime_redirection_kind;

/** Borrowed descriptor action prepared by the evaluator. */
typedef struct wsh_runtime_redirection {
    /** Action applied at this point in left-to-right order. */
    wsh_runtime_redirection_kind kind;
    /** Destination logical descriptor in the range 0 through 9. */
    unsigned target_descriptor;
    /** Source descriptor for duplicate, otherwise zero. */
    unsigned source_descriptor;
    /** Expanded path or normalized here text, otherwise empty. */
    wsh_string_view operand;
} wsh_runtime_redirection;

/** Borrowed fully expanded external command. */
typedef struct wsh_runtime_command {
    /** Unresolved executable subject. */
    wsh_string_view subject;
    /** Structured arguments excluding the executable, or null. */
    const wsh_value *arguments;
    /** Ordered descriptor actions, or null. */
    const wsh_runtime_redirection *redirections;
    /** Number of descriptor actions. */
    size_t redirection_count;
    /** Complete caller-supplied command line for raw launch. */
    wsh_string_view raw_command_line;
    /** Nonzero only for explicit policy-controlled raw launch. */
    int raw;
    /** Nonzero asks the runtime to execute the shell `echo` stage. */
    int shell_echo;
} wsh_runtime_command;

/** Borrowed descriptor connection between adjacent pipeline stages. */
typedef struct wsh_runtime_pipeline_edge {
    /** Logical descriptor supplied by the left stage. */
    unsigned output_descriptor;
    /** Logical descriptor consumed by the right stage. */
    unsigned input_descriptor;
} wsh_runtime_pipeline_edge;

/** Launch-plan mode flags. */
typedef enum wsh_runtime_launch_flag {
    /** Return after registering launched background work. */
    WSH_RUNTIME_LAUNCH_BACKGROUND = 1,
    /** Capture the final stage's descriptor 1 into runtime output. */
    WSH_RUNTIME_LAUNCH_CAPTURE = 2,
    /** Create a cancellable Windows process group. */
    WSH_RUNTIME_LAUNCH_PROCESS_GROUP = 4
} wsh_runtime_launch_flag;

/** Borrowed validated composition plan for one runtime request. */
typedef struct wsh_runtime_launch_plan {
    /** Commands in source order. */
    const wsh_runtime_command *commands;
    /** Number of commands, at least one for launch. */
    size_t command_count;
    /** Connections between adjacent commands. */
    const wsh_runtime_pipeline_edge *edges;
    /** Number of edges, command_count minus one for a pipeline. */
    size_t edge_count;
    /** Bitwise combination of wsh_runtime_launch_flag values. */
    unsigned flags;
    /** Finite wait deadline in milliseconds, or zero for no deadline. */
    uint32_t timeout_milliseconds;
} wsh_runtime_launch_plan;

/** Operation categories understood by an abstract runtime. */
typedef enum wsh_runtime_operation {
    /** Read source bytes identified by the subject. */
    WSH_RUNTIME_READ_SOURCE = 1,
    /** Write bytes to a logical stream or file. */
    WSH_RUNTIME_WRITE = 2,
    /** Launch a structured external operation. */
    WSH_RUNTIME_LAUNCH = 3,
    /** Query a deterministic clock value. */
    WSH_RUNTIME_CLOCK = 4,
    /** Query or update environment-boundary state. */
    WSH_RUNTIME_ENVIRONMENT = 5,
    /** Return candidate filesystem paths for one unquoted pattern. */
    WSH_RUNTIME_MATCH_PATHS = 6,
    /** Launch an ordered pipeline described by a typed plan. */
    WSH_RUNTIME_PIPELINE = 7,
    /** Wait for selected or all registered background work. */
    WSH_RUNTIME_WAIT = 8,
    /** Query or change the runtime's logical working directory. */
    WSH_RUNTIME_WORKING_DIRECTORY = 9,
    /** Create a named-pipe provider and return its path. */
    WSH_RUNTIME_PROCESS_SUBSTITUTION = 10,
    /** Request bounded cancellation of foreground or selected work. */
    WSH_RUNTIME_CANCEL = 11
} wsh_runtime_operation;

/** One immutable request crossing the runtime boundary. */
typedef struct wsh_runtime_request {
    /** Requested category of effect or query. */
    wsh_runtime_operation operation;
    /** Borrowed primary path, stream name, command, or query name. */
    wsh_string_view subject;
    /** Borrowed structured arguments, which may be null. */
    const wsh_value *arguments;
    /** Borrowed isolated context supplying exported launch state. */
    const wsh_context *context;
    /** Borrowed typed launch plan for orchestration operations. */
    const wsh_runtime_launch_plan *launch_plan;
} wsh_runtime_request;

/** Perform one abstract operation using caller-owned output builders. */
typedef wsh_result (*wsh_runtime_invoke_fn)(
    void *user_data,
    const wsh_runtime_request *request,
    wsh_value_builder *output,
    wsh_status_builder *status);

/** Compare two exported names using the host's Windows boundary rules. */
typedef int (*wsh_runtime_names_equal_fn)(
    void *user_data,
    wsh_string_view left,
    wsh_string_view right);

/** Runtime callbacks copied into a context. */
typedef struct wsh_runtime {
    /** Opaque value passed to runtime callbacks. */
    void *user_data;
    /** Required operation callback when a runtime call is attempted. */
    wsh_runtime_invoke_fn invoke;
    /** Optional Windows-insensitive exported-name comparator. */
    wsh_runtime_names_equal_fn names_equal;
} wsh_runtime;

/** Diagnostic severity. */
typedef enum wsh_diagnostic_severity {
    /** Informational context that does not itself fail an operation. */
    WSH_DIAGNOSTIC_NOTE = 0,
    /** Recoverable or advisory condition. */
    WSH_DIAGNOSTIC_WARNING = 1,
    /** Error that prevents the requested operation. */
    WSH_DIAGNOSTIC_ERROR = 2
} wsh_diagnostic_severity;

/** Stable portable-core diagnostic code. */
typedef enum wsh_diagnostic_code {
    /** Invalid encoded source or text. */
    WSH_DIAGNOSTIC_INVALID_ENCODING = 1001,
    /** Configured resource ceiling was reached. */
    WSH_DIAGNOSTIC_LIMIT = 1002,
    /** Exported names collide under Windows comparison. */
    WSH_DIAGNOSTIC_EXPORT_COLLISION = 1003,
    /** Abstract runtime operation failed or mismatched. */
    WSH_DIAGNOSTIC_RUNTIME = 1004,
    /** Caller supplied an invalid core argument. */
    WSH_DIAGNOSTIC_INVALID_ARGUMENT = 1005,
    /** Evaluator rejected a semantic operation. */
    WSH_DIAGNOSTIC_EVALUATION = 3001,
    /** Expansion or substitution could not produce a bounded value. */
    WSH_DIAGNOSTIC_EXPANSION = 3002,
    /** A control transfer was invalid in the current dynamic context. */
    WSH_DIAGNOSTIC_CONTROL = 3003
} wsh_diagnostic_code;

/** Borrowed diagnostic data owned by a context. */
typedef struct wsh_diagnostic_view {
    /** Diagnostic severity. */
    wsh_diagnostic_severity severity;
    /** Stable diagnostic code. */
    wsh_diagnostic_code code;
    /** Validated UTF-8 message. */
    wsh_string_view message;
    /** Optional validated UTF-8 source name. */
    wsh_string_view source_name;
    /** Nonzero when span contains a source range. */
    int has_span;
    /** Source range when has_span is nonzero. */
    wsh_source_span span;
} wsh_diagnostic_view;

/** Options used to create one context. */
typedef struct wsh_context_options {
    /** Allocator used for all context-owned objects. */
    wsh_allocator allocator;
    /** Resource limits copied into the context. */
    wsh_limits limits;
    /** Optional abstract runtime copied into the context. */
    wsh_runtime runtime;
} wsh_context_options;

/** Opaque deterministic runtime test double. */
typedef struct wsh_fake_runtime wsh_fake_runtime;

/**
 * Return the portable default allocator.
 * @return Allocator backed by malloc and free.
 */
wsh_allocator wsh_allocator_default(void);

/**
 * Release memory with a specified allocator.
 * @param allocator Allocator that created pointer.
 * @param pointer Pointer to release; null is accepted.
 */
void wsh_allocator_release(
    const wsh_allocator *allocator,
    void *pointer);

/**
 * Return conservative default resource limits.
 * @return Fully initialized limit set.
 */
wsh_limits wsh_limits_default(void);

/**
 * Build a string view from a NUL-terminated C string.
 * @param text C string, or null for an empty view.
 * @return Borrowed view excluding the terminator.
 */
wsh_string_view wsh_string_view_from_cstr(const char *text);

/**
 * Compare two byte-exact string views.
 * @param left First view.
 * @param right Second view.
 * @return Nonzero when lengths and bytes are equal.
 */
int wsh_string_view_equal(
    wsh_string_view left,
    wsh_string_view right);

/**
 * Strictly validate one internal UTF-8 view.
 * @param text Input bytes; U+0000 and noncharacters are rejected.
 * @param out_scalar_count Optional output for decoded scalar count.
 * @return WSH_OK or a validation error.
 */
wsh_result wsh_utf8_validate(
    wsh_string_view text,
    size_t *out_scalar_count);

/**
 * Convert strict UTF-8 to native-endian UTF-16 units.
 * @param allocator Allocator for the returned buffer, or null for default.
 * @param limits Limits for input and output.
 * @param input Strict UTF-8 input.
 * @param out_units Receives an owned, zero-terminated unit buffer.
 * @param out_length Receives units excluding the terminator.
 * @return WSH_OK or an encoding/resource error.
 */
wsh_result wsh_utf8_to_utf16(
    const wsh_allocator *allocator,
    const wsh_limits *limits,
    wsh_string_view input,
    uint16_t **out_units,
    size_t *out_length);

/**
 * Convert native-endian UTF-16 units to strict UTF-8.
 * @param allocator Allocator for the returned buffer, or null for default.
 * @param limits Limits for input and output.
 * @param units UTF-16 input units.
 * @param length Number of input units.
 * @param out_bytes Receives an owned, zero-terminated UTF-8 buffer.
 * @param out_length Receives bytes excluding the terminator.
 * @return WSH_OK or an encoding/resource error.
 */
wsh_result wsh_utf16_to_utf8(
    const wsh_allocator *allocator,
    const wsh_limits *limits,
    const uint16_t *units,
    size_t length,
    char **out_bytes,
    size_t *out_length);

/**
 * Detect a leading Unicode byte-order mark.
 * @param bytes Raw source bytes.
 * @param length Number of raw bytes.
 * @return Recognized BOM or WSH_BOM_NONE.
 */
wsh_bom_kind wsh_source_detect_bom(
    const unsigned char *bytes,
    size_t length);

/**
 * Decode, validate, and normalize a complete source buffer.
 * @param allocator Object allocator, or null for default.
 * @param limits Source limits, or null for defaults.
 * @param bytes Raw source bytes; null is accepted only at length zero.
 * @param length Raw byte length.
 * @param out_source Receives the owned source on success.
 * @return WSH_OK or an encoding/resource/argument error.
 */
wsh_result wsh_source_create(
    const wsh_allocator *allocator,
    const wsh_limits *limits,
    const unsigned char *bytes,
    size_t length,
    wsh_source **out_source);

/**
 * Destroy a decoded source.
 * @param source Owned source; null is accepted.
 */
void wsh_source_destroy(wsh_source *source);

/**
 * Return normalized internal UTF-8 source text.
 * @param source Source owner.
 * @return Borrowed view, empty for an invalid source pointer.
 */
wsh_string_view wsh_source_text(const wsh_source *source);

/**
 * Return the source's leading BOM classification.
 * @param source Source owner.
 * @return BOM kind, or WSH_BOM_NONE for null.
 */
wsh_bom_kind wsh_source_bom(const wsh_source *source);

/**
 * Return decoded Unicode-scalar count.
 * @param source Source owner.
 * @return Scalar count, or zero for null.
 */
size_t wsh_source_scalar_count(const wsh_source *source);

/**
 * Return logical line count, including a final unterminated line.
 * @param source Source owner.
 * @return At least one for a valid source, or zero for null.
 */
size_t wsh_source_line_count(const wsh_source *source);

/**
 * Resolve a normalized UTF-8 byte range to original and scalar positions.
 * @param source Source owner.
 * @param utf8_offset Inclusive normalized UTF-8 start offset.
 * @param utf8_length Range byte length.
 * @param out_span Receives the half-open span.
 * @return WSH_OK, or WSH_ERR_INVALID for a non-scalar boundary.
 */
wsh_result wsh_source_get_span(
    const wsh_source *source,
    size_t utf8_offset,
    size_t utf8_length,
    wsh_source_span *out_span);

/**
 * Create an immutable string by copying strict UTF-8.
 * @param allocator Object allocator, or null for default.
 * @param limits String limits, or null for defaults.
 * @param text Strict UTF-8 input.
 * @param out_string Receives the owned string.
 * @return WSH_OK or an encoding/resource/argument error.
 */
wsh_result wsh_string_create(
    const wsh_allocator *allocator,
    const wsh_limits *limits,
    wsh_string_view text,
    wsh_string **out_string);

/**
 * Destroy an immutable string.
 * @param string Owned string; null is accepted.
 */
void wsh_string_destroy(wsh_string *string);

/**
 * Borrow an immutable string's bytes.
 * @param string String owner.
 * @return Borrowed view, or empty for null.
 */
wsh_string_view wsh_string_bytes(const wsh_string *string);

/**
 * Create an empty fault-atomic string builder.
 * @param allocator Builder and output allocator, or null for default.
 * @param limits String limits, or null for defaults.
 * @param out_builder Receives the owned builder.
 * @return WSH_OK or an argument/resource error.
 */
wsh_result wsh_string_builder_create(
    const wsh_allocator *allocator,
    const wsh_limits *limits,
    wsh_string_builder **out_builder);

/**
 * Append strict UTF-8 to a string builder.
 * @param builder Mutable unpublished builder.
 * @param text Bytes to append.
 * @return WSH_OK or an encoding/resource/argument error.
 */
wsh_result wsh_string_builder_append(
    wsh_string_builder *builder,
    wsh_string_view text);

/**
 * Publish a builder's bytes as one immutable string.
 * @param builder Mutable unpublished builder.
 * @param out_string Receives the owned immutable string.
 * @return WSH_OK or an argument/resource error.
 */
wsh_result wsh_string_builder_finish(
    wsh_string_builder *builder,
    wsh_string **out_string);

/**
 * Destroy a string builder and any unpublished bytes.
 * @param builder Owned builder; null is accepted.
 */
void wsh_string_builder_destroy(wsh_string_builder *builder);

/**
 * Create an empty flat-list builder.
 * @param allocator Builder and output allocator, or null for default.
 * @param limits List limits, or null for defaults.
 * @param out_builder Receives the owned builder.
 * @return WSH_OK or an argument/resource error.
 */
wsh_result wsh_value_builder_create(
    const wsh_allocator *allocator,
    const wsh_limits *limits,
    wsh_value_builder **out_builder);

/**
 * Append one copied immutable string element.
 * @param builder Mutable unpublished builder.
 * @param text Strict UTF-8 element.
 * @return WSH_OK or an encoding/resource/argument error.
 */
wsh_result wsh_value_builder_append(
    wsh_value_builder *builder,
    wsh_string_view text);

/**
 * Publish a builder as an immutable flat value.
 * @param builder Mutable unpublished builder.
 * @param out_value Receives the owned value.
 * @return WSH_OK or an argument/resource error.
 */
wsh_result wsh_value_builder_finish(
    wsh_value_builder *builder,
    wsh_value **out_value);

/**
 * Destroy a value builder and unpublished elements.
 * @param builder Owned builder; null is accepted.
 */
void wsh_value_builder_destroy(wsh_value_builder *builder);

/**
 * Deep-clone an immutable flat value.
 * @param allocator Allocator for the clone, or null for default.
 * @param limits Limits for the clone, or null for defaults.
 * @param value Value to clone.
 * @param out_value Receives the owned clone.
 * @return WSH_OK or an argument/resource error.
 */
wsh_result wsh_value_clone(
    const wsh_allocator *allocator,
    const wsh_limits *limits,
    const wsh_value *value,
    wsh_value **out_value);

/**
 * Destroy an immutable flat value.
 * @param value Owned value; null is accepted.
 */
void wsh_value_destroy(wsh_value *value);

/**
 * Return the number of strings in a flat value.
 * @param value Value owner.
 * @return Element count, or zero for null.
 */
size_t wsh_value_count(const wsh_value *value);

/**
 * Borrow one string element by index.
 * @param value Value owner.
 * @param index Zero-based index.
 * @param out_text Receives a borrowed view.
 * @return WSH_OK or WSH_ERR_INVALID for an invalid index.
 */
wsh_result wsh_value_at(
    const wsh_value *value,
    size_t index,
    wsh_string_view *out_text);

/**
 * Create an empty status-list builder.
 * @param allocator Builder and output allocator, or null for default.
 * @param limits Item limits, or null for defaults.
 * @param out_builder Receives the owned builder.
 * @return WSH_OK or an argument/resource error.
 */
wsh_result wsh_status_builder_create(
    const wsh_allocator *allocator,
    const wsh_limits *limits,
    wsh_status_builder **out_builder);

/**
 * Append one unsigned status.
 * @param builder Mutable unpublished builder.
 * @param status Unsigned Windows status value.
 * @return WSH_OK or an argument/resource error.
 */
wsh_result wsh_status_builder_append(
    wsh_status_builder *builder,
    uint32_t status);

/**
 * Publish a builder as an immutable status list.
 * @param builder Mutable unpublished builder.
 * @param out_status Receives the owned list.
 * @return WSH_OK or an argument/resource error.
 */
wsh_result wsh_status_builder_finish(
    wsh_status_builder *builder,
    wsh_status_list **out_status);

/**
 * Destroy a status builder and unpublished elements.
 * @param builder Owned builder; null is accepted.
 */
void wsh_status_builder_destroy(wsh_status_builder *builder);

/**
 * Destroy an immutable status list.
 * @param status Owned list; null is accepted.
 */
void wsh_status_list_destroy(wsh_status_list *status);

/**
 * Return the number of statuses.
 * @param status Status owner.
 * @return Element count, or zero for null.
 */
size_t wsh_status_list_count(const wsh_status_list *status);

/**
 * Return one status by index.
 * @param status Status owner.
 * @param index Zero-based index.
 * @param out_value Receives the status.
 * @return WSH_OK or WSH_ERR_INVALID for an invalid index.
 */
wsh_result wsh_status_list_at(
    const wsh_status_list *status,
    size_t index,
    uint32_t *out_value);

/**
 * Test the all-zero truth rule.
 * @param status Status owner.
 * @return Nonzero only for a nonempty, all-zero list.
 */
int wsh_status_list_is_success(const wsh_status_list *status);

/**
 * Return the last status when present.
 * @param status Status owner.
 * @param out_value Receives the last status.
 * @return WSH_OK, or WSH_ERR_INVALID for an empty or null list.
 */
wsh_result wsh_status_list_last(
    const wsh_status_list *status,
    uint32_t *out_value);

/**
 * Initialize context options with portable defaults.
 * @param out_options Receives complete defaults.
 */
void wsh_context_options_init(wsh_context_options *out_options);

/**
 * Create one isolated portable context.
 * @param options Options, or null for defaults.
 * @param out_context Receives the owned context.
 * @return WSH_OK or an argument/resource error.
 */
wsh_result wsh_context_create(
    const wsh_context_options *options,
    wsh_context **out_context);

/**
 * Destroy an idle context and all state it owns.
 * @param context Owned context; null is accepted.
 */
void wsh_context_destroy(wsh_context *context);

/**
 * Set an exact case-sensitive variable as private when newly created.
 * @param context Context owner.
 * @param name Validated nonempty UTF-8 name.
 * @param value Immutable flat value to clone.
 * @return WSH_OK or an argument/resource error.
 */
wsh_result wsh_context_set_variable(
    wsh_context *context,
    wsh_string_view name,
    const wsh_value *value);

/**
 * Import a variable and mark it exported.
 * @param context Context owner.
 * @param name Validated nonempty UTF-8 name.
 * @param value Immutable flat value to clone.
 * @return WSH_OK, collision, or an argument/resource error.
 */
wsh_result wsh_context_import_variable(
    wsh_context *context,
    wsh_string_view name,
    const wsh_value *value);

/**
 * Borrow a variable value by exact case-sensitive name.
 * @param context Context owner.
 * @param name Name to find.
 * @param out_value Receives a borrowed value pointer.
 * @return WSH_OK or WSH_ERR_INVALID when absent.
 */
wsh_result wsh_context_get_variable(
    const wsh_context *context,
    wsh_string_view name,
    const wsh_value **out_value);

/**
 * Remove an exact case-sensitive variable.
 * @param context Context owner.
 * @param name Name to remove.
 * @return WSH_OK or WSH_ERR_INVALID when absent.
 */
wsh_result wsh_context_unset_variable(
    wsh_context *context,
    wsh_string_view name);

/**
 * Change export metadata after checking boundary-name uniqueness.
 * @param context Context owner.
 * @param name Exact case-sensitive variable name.
 * @param exported Nonzero to export, zero to make private.
 * @return WSH_OK, mismatch for a collision, or an argument error.
 */
wsh_result wsh_context_set_exported(
    wsh_context *context,
    wsh_string_view name,
    int exported);

/**
 * Read export metadata for an exact variable.
 * @param context Context owner.
 * @param name Exact case-sensitive variable name.
 * @param out_exported Receives zero or one.
 * @return WSH_OK or WSH_ERR_INVALID when absent.
 */
wsh_result wsh_context_is_exported(
    const wsh_context *context,
    wsh_string_view name,
    int *out_exported);

/**
 * Return the number of context variables.
 * @param context Context owner.
 * @return Variable count, or zero for null.
 */
size_t wsh_context_variable_count(const wsh_context *context);

/**
 * Inspect one variable in deterministic insertion order.
 * @param context Context owner.
 * @param index Zero-based variable index.
 * @param out_name Receives the borrowed exact name.
 * @param out_value Receives the borrowed flat value.
 * @param out_exported Receives zero or one.
 * @return WSH_OK or WSH_ERR_INVALID for an invalid index.
 */
wsh_result wsh_context_variable_at(
    const wsh_context *context,
    size_t index,
    wsh_string_view *out_name,
    const wsh_value **out_value,
    int *out_exported);

/**
 * Copy the allocator, limits, and runtime used by a context.
 * @param context Context owner.
 * @param out_options Receives copied options.
 * @return WSH_OK or WSH_ERR_INVALID.
 */
wsh_result wsh_context_get_options(
    const wsh_context *context,
    wsh_context_options *out_options);

/**
 * Add one bounded structured diagnostic by copying its text.
 * @param context Context owner.
 * @param severity Diagnostic severity.
 * @param code Stable diagnostic code.
 * @param message Validated message text.
 * @param source_name Optional validated source name.
 * @param span Optional source span.
 * @return WSH_OK or an encoding/resource/argument error.
 */
wsh_result wsh_context_add_diagnostic(
    wsh_context *context,
    wsh_diagnostic_severity severity,
    wsh_diagnostic_code code,
    wsh_string_view message,
    wsh_string_view source_name,
    const wsh_source_span *span);

/**
 * Return the number of retained diagnostics.
 * @param context Context owner.
 * @return Diagnostic count, or zero for null.
 */
size_t wsh_context_diagnostic_count(const wsh_context *context);

/**
 * Borrow one retained diagnostic.
 * @param context Context owner.
 * @param index Zero-based queue index.
 * @param out_view Receives borrowed diagnostic fields.
 * @return WSH_OK or WSH_ERR_INVALID for an invalid index.
 */
wsh_result wsh_context_diagnostic_at(
    const wsh_context *context,
    size_t index,
    wsh_diagnostic_view *out_view);

/**
 * Invoke the configured abstract runtime with bounded output.
 * @param context Context owner.
 * @param request Immutable request.
 * @param out_value Receives an owned output value.
 * @param out_status Receives an owned status list.
 * @return Runtime result or an argument/resource error.
 */
wsh_result wsh_context_runtime_invoke(
    wsh_context *context,
    const wsh_runtime_request *request,
    wsh_value **out_value,
    wsh_status_list **out_status);

/**
 * Create an empty deterministic fake runtime.
 * @param allocator Runtime allocator, or null for default.
 * @param limits Runtime limits, or null for defaults.
 * @param out_fake Receives the owned fake.
 * @return WSH_OK or an argument/resource error.
 */
wsh_result wsh_fake_runtime_create(
    const wsh_allocator *allocator,
    const wsh_limits *limits,
    wsh_fake_runtime **out_fake);

/**
 * Destroy a fake runtime and all scripted expectations.
 * @param fake Owned fake; null is accepted.
 */
void wsh_fake_runtime_destroy(wsh_fake_runtime *fake);

/**
 * Return callbacks bound to a fake runtime.
 * @param fake Fake owner.
 * @return Runtime callbacks; invoke is null when fake is null.
 */
wsh_runtime wsh_fake_runtime_interface(wsh_fake_runtime *fake);

/**
 * Append one ordered fake-runtime expectation.
 * @param fake Fake owner.
 * @param operation Expected operation category.
 * @param subject Expected byte-exact subject.
 * @param output Optional scripted output value.
 * @param status Optional scripted status list.
 * @param result Result returned after scripted output is copied.
 * @return WSH_OK or an argument/resource error.
 */
wsh_result wsh_fake_runtime_expect(
    wsh_fake_runtime *fake,
    wsh_runtime_operation operation,
    wsh_string_view subject,
    const wsh_value *output,
    const wsh_status_list *status,
    wsh_result result);

/**
 * Append an expectation that also requires exact structured arguments.
 * @param fake Fake owner.
 * @param operation Expected operation category.
 * @param subject Expected byte-exact subject.
 * @param arguments Required byte-exact ordered arguments.
 * @param output Optional scripted output value.
 * @param status Optional scripted status list.
 * @param result Result returned after scripted output is copied.
 * @return WSH_OK or an argument/resource error.
 */
wsh_result wsh_fake_runtime_expect_arguments(
    wsh_fake_runtime *fake,
    wsh_runtime_operation operation,
    wsh_string_view subject,
    const wsh_value *arguments,
    const wsh_value *output,
    const wsh_status_list *status,
    wsh_result result);

/**
 * Verify that every scripted expectation was consumed.
 * @param fake Fake owner.
 * @return WSH_OK when complete, otherwise WSH_ERR_MISMATCH.
 */
wsh_result wsh_fake_runtime_complete(const wsh_fake_runtime *fake);

/**
 * Return the number of calls observed by a fake runtime.
 * @param fake Fake owner.
 * @return Observed call count, or zero for null.
 */
size_t wsh_fake_runtime_call_count(const wsh_fake_runtime *fake);

#ifdef __cplusplus
}
#endif

#endif
