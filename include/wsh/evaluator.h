/**
 * @file evaluator.h
 * @brief Bounded portable evaluation of complete WSH parse trees.
 */

#ifndef WSH_EVALUATOR_H
#define WSH_EVALUATOR_H

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
void wsh_evaluator_options_init(wsh_evaluator_options *out_options);

/**
 * Create an evaluator borrowing one isolated context.
 * @param context Context borrowed until evaluator destruction.
 * @param options Options, or null for defaults derived from the context.
 * @param out_evaluator Receives the owned evaluator.
 * @return WSH_OK or an argument/resource error.
 */
wsh_result wsh_evaluator_create(
    wsh_context *context,
    const wsh_evaluator_options *options,
    wsh_evaluator **out_evaluator);

/** Destroy an evaluator and its persistent functions. */
void wsh_evaluator_destroy(wsh_evaluator *evaluator);

/**
 * Evaluate one complete immutable parse tree.
 * @param evaluator Evaluator owner.
 * @param tree Complete parse tree borrowed for the call.
 * @param out_status Receives an owned nonempty status list.
 * @return WSH_OK or an evaluation/resource/runtime error.
 */
wsh_result wsh_evaluate(
    wsh_evaluator *evaluator,
    const wsh_parse_tree *tree,
    wsh_status_list **out_status);

/** Return whether the `~` matcher accepts text for pattern. */
int wsh_pattern_matches(
    wsh_string_view pattern,
    wsh_string_view text);

#ifdef __cplusplus
}
#endif

#endif
