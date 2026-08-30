/**
 * @file evaluator.h
 * @brief Bounded portable evaluation of complete WSH parse trees.
 */

#ifndef WSH_EVALUATOR_H
#define WSH_EVALUATOR_H

#include "wsh/api.h"

#include "wsh/parser.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Opaque evaluator retaining functions and dynamic semantic state. */
typedef struct wsh_evaluator wsh_evaluator;

/** Finite evaluator-specific ceilings and copied object options. */
typedef struct wsh_evaluator_options {
    /** Allocator for evaluator-owned data. */
    wsh_allocator allocator;
    /** Core value/string ceilings. */
    wsh_limits limits;
    /** Maximum AST/control steps in one public evaluation. */
    size_t max_steps;
    /** Maximum nested evaluation/function/substitution depth. */
    size_t max_depth;
    /** Logical source name copied into semantic diagnostics. */
    wsh_string_view source_name;
} wsh_evaluator_options;

/** Initialize conservative finite evaluator defaults. */
WSH_API void wsh_evaluator_options_init(wsh_evaluator_options *out_options);

/**
 * Create an evaluator borrowing one isolated context.
 * @param context Context borrowed until evaluator destruction.
 * @param options Options, or null for defaults derived from the context.
 * @param out_evaluator Receives the owned evaluator.
 * @return WSH_OK or an argument/resource error.
 */
WSH_API wsh_result wsh_evaluator_create(
    wsh_context *context,
    const wsh_evaluator_options *options,
    wsh_evaluator **out_evaluator);

/** Destroy an evaluator and its persistent functions. */
WSH_API void wsh_evaluator_destroy(wsh_evaluator *evaluator);

/**
 * Evaluate one complete immutable parse tree.
 * @param evaluator Evaluator owner.
 * @param tree Complete parse tree borrowed for the call.
 * @param out_status Receives an owned nonempty status list.
 * @return WSH_OK or an evaluation/resource/runtime error.
 */
WSH_API wsh_result wsh_evaluate(
    wsh_evaluator *evaluator,
    const wsh_parse_tree *tree,
    wsh_status_list **out_status);

/** Return the number of persistent functions in definition order. */
WSH_API size_t wsh_evaluator_function_count(const wsh_evaluator *evaluator);

/** Borrow one persistent function name by zero-based index. */
WSH_API wsh_result wsh_evaluator_function_at(
    const wsh_evaluator *evaluator,
    size_t index,
    wsh_string_view *out_name);

/**
 * Publish a default status and invoke one signal function when defined.
 * @param evaluator Evaluator owner.
 * @param name Exact function name such as `sigint` or `sigexit`.
 * @param default_status Status published before optional invocation.
 * @param out_status Receives the owned final status.
 * @return WSH_OK or an evaluation/resource error.
 */
WSH_API wsh_result wsh_evaluator_invoke_signal(
    wsh_evaluator *evaluator,
    wsh_string_view name,
    uint32_t default_status,
    wsh_status_list **out_status);

/** Return nonzero after the evaluator accepted an `exit` built-in. */
WSH_API int wsh_evaluator_exit_requested(
    const wsh_evaluator *evaluator,
    uint32_t *out_status,
    int *out_forced);

/** Clear a refused interactive exit request. */
WSH_API void wsh_evaluator_clear_exit(wsh_evaluator *evaluator);

/** Return whether the `~` matcher accepts text for pattern. */
WSH_API int wsh_pattern_matches(
    wsh_string_view pattern,
    wsh_string_view text);

#ifdef __cplusplus
}
#endif

#endif
