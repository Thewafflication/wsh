/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file evaluator_tests.c
 * @brief Controlled M4 evaluator and semantic-conformance tests.
 */

#include "wsh/evaluator.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/** Fail the current controlled test when condition is false. */
#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "CHECK failed at %s:%d: %s\n", \
            __FILE__, __LINE__, #condition); \
        return 0; \
    } \
} while (0)

/** Owned fake-runtime evaluator fixture. */
typedef struct test_session {
    /** Deterministic runtime owner. */
    wsh_fake_runtime *fake;
    /** Isolated semantic context owner. */
    wsh_context *context;
    /** Evaluator owner. */
    wsh_evaluator *evaluator;
} test_session;

/** Allocation counter with one optional failing ordinal. */
typedef struct fault_allocator {
    /** Allocation calls observed. */
    size_t calls;
    /** One-origin allocation ordinal to fail, or zero. */
    size_t fail_at;
    /** Successfully allocated blocks not yet released. */
    size_t outstanding;
} fault_allocator;

/** Allocate through a deterministic fault-injection counter. */
static void *fault_allocate(void *user_data, size_t size)
{
    fault_allocator *state = (fault_allocator *)user_data;
    void *pointer;

    state->calls += 1U;
    if (state->fail_at != 0U && state->calls == state->fail_at) {
        return NULL;
    }
    pointer = malloc(size == 0U ? 1U : size);
    if (pointer != NULL) {
        state->outstanding += 1U;
    }
    return pointer;
}

/** Release one block through the fault-injection counter. */
static void fault_deallocate(void *user_data, void *pointer)
{
    fault_allocator *state = (fault_allocator *)user_data;

    if (pointer != NULL) {
        state->outstanding -= 1U;
        free(pointer);
    }
}

/** Build one immutable test value from C strings. */
static wsh_result make_value(
    const wsh_allocator *allocator,
    const char *const *items,
    size_t count,
    wsh_value **out_value)
{
    wsh_value_builder *builder;
    size_t index;
    wsh_result result;

    *out_value = NULL;
    result = wsh_value_builder_create(allocator, NULL, &builder);
    for (index = 0U; result == WSH_OK && index < count; ++index) {
        result = wsh_value_builder_append(
            builder, wsh_string_view_from_cstr(items[index]));
    }
    if (result == WSH_OK) {
        result = wsh_value_builder_finish(builder, out_value);
    }
    wsh_value_builder_destroy(builder);
    return result;
}

/** Build one immutable test status from unsigned elements. */
static wsh_result make_status(
    const wsh_allocator *allocator,
    const uint32_t *items,
    size_t count,
    wsh_status_list **out_status)
{
    wsh_status_builder *builder;
    size_t index;
    wsh_result result;

    *out_status = NULL;
    result = wsh_status_builder_create(allocator, NULL, &builder);
    for (index = 0U; result == WSH_OK && index < count; ++index) {
        result = wsh_status_builder_append(builder, items[index]);
    }
    if (result == WSH_OK) {
        result = wsh_status_builder_finish(builder, out_status);
    }
    wsh_status_builder_destroy(builder);
    return result;
}

/** Create a default fake-runtime evaluator fixture. */
static int start_session(test_session *session)
{
    wsh_context_options context_options;
    wsh_evaluator_options evaluator_options;

    memset(session, 0, sizeof(*session));
    CHECK(wsh_fake_runtime_create(NULL, NULL, &session->fake) == WSH_OK);
    wsh_context_options_init(&context_options);
    context_options.runtime = wsh_fake_runtime_interface(session->fake);
    CHECK(wsh_context_create(&context_options, &session->context) == WSH_OK);
    wsh_evaluator_options_init(&evaluator_options);
    evaluator_options.source_name = wsh_string_view_from_cstr("m4-test.wsh");
    CHECK(wsh_evaluator_create(
        session->context, &evaluator_options, &session->evaluator) == WSH_OK);
    return 1;
}

/** Destroy a fake-runtime evaluator fixture. */
static void stop_session(test_session *session)
{
    wsh_evaluator_destroy(session->evaluator);
    wsh_context_destroy(session->context);
    wsh_fake_runtime_destroy(session->fake);
    memset(session, 0, sizeof(*session));
}

/** Script one exact fake-runtime request and response. */
static wsh_result expect_call(
    test_session *session,
    wsh_runtime_operation operation,
    const char *subject,
    const char *const *arguments,
    size_t argument_count,
    const char *const *output,
    size_t output_count,
    const uint32_t *statuses,
    size_t status_count,
    wsh_result result_code)
{
    wsh_value *argument_value;
    wsh_value *output_value;
    wsh_status_list *status;
    wsh_result result;

    argument_value = NULL;
    output_value = NULL;
    status = NULL;
    result = make_value(NULL, arguments, argument_count, &argument_value);
    if (result == WSH_OK) {
        result = make_value(NULL, output, output_count, &output_value);
    }
    if (result == WSH_OK) {
        result = make_status(NULL, statuses, status_count, &status);
    }
    if (result == WSH_OK) {
        result = wsh_fake_runtime_expect_arguments(
            session->fake,
            operation,
            wsh_string_view_from_cstr(subject),
            argument_value,
            output_value,
            status,
            result_code);
    }
    wsh_value_destroy(argument_value);
    wsh_value_destroy(output_value);
    wsh_status_list_destroy(status);
    return result;
}

/** Script a fake-runtime response without argument comparison. */
static wsh_result expect_subject(
    test_session *session,
    wsh_runtime_operation operation,
    const char *subject,
    const char *const *output,
    size_t output_count,
    const uint32_t *statuses,
    size_t status_count)
{
    wsh_value *output_value;
    wsh_status_list *status;
    wsh_result result;

    output_value = NULL;
    status = NULL;
    result = make_value(NULL, output, output_count, &output_value);
    if (result == WSH_OK) {
        result = make_status(NULL, statuses, status_count, &status);
    }
    if (result == WSH_OK) {
        result = wsh_fake_runtime_expect(
            session->fake,
            operation,
            wsh_string_view_from_cstr(subject),
            output_value,
            status,
            WSH_OK);
    }
    wsh_value_destroy(output_value);
    wsh_status_list_destroy(status);
    return result;
}

/** Decode, parse, and evaluate one complete test source. */
static wsh_result evaluate_text(
    wsh_evaluator *evaluator,
    const char *text,
    wsh_status_list **out_status)
{
    wsh_source *source;
    wsh_parse_tree *tree;
    wsh_result result;

    *out_status = NULL;
    source = NULL;
    tree = NULL;
    result = wsh_source_create(
        NULL, NULL, (const unsigned char *)text, strlen(text), &source);
    if (result == WSH_OK) {
        result = wsh_parse(NULL, source, &tree);
    }
    if (result == WSH_OK &&
        wsh_parse_tree_status(tree) != WSH_SYNTAX_COMPLETE) {
        result = WSH_ERR_INVALID;
    }
    if (result == WSH_OK) {
        result = wsh_evaluate(evaluator, tree, out_status);
    }
    wsh_parse_tree_destroy(tree);
    wsh_source_destroy(source);
    return result;
}

/** Compare a flat value with exact expected C strings. */
static int value_equals(
    const wsh_value *value,
    const char *const *expected,
    size_t count)
{
    size_t index;
    wsh_string_view item;

    if (value == NULL || wsh_value_count(value) != count) {
        return 0;
    }
    for (index = 0U; index < count; ++index) {
        if (wsh_value_at(value, index, &item) != WSH_OK ||
            !wsh_string_view_equal(
                item, wsh_string_view_from_cstr(expected[index]))) {
            return 0;
        }
    }
    return 1;
}

/** Compare a status list with exact expected elements. */
static int status_equals(
    const wsh_status_list *status,
    const uint32_t *expected,
    size_t count)
{
    size_t index;
    uint32_t item;

    if (status == NULL || wsh_status_list_count(status) != count) {
        return 0;
    }
    for (index = 0U; index < count; ++index) {
        if (wsh_status_list_at(status, index, &item) != WSH_OK ||
            item != expected[index]) {
            return 0;
        }
    }
    return 1;
}

/** Verify flat values, special expansions, and deterministic globbing. */
static int test_tc_0007(void)
{
    test_session session;
    wsh_status_list *status;
    const uint32_t ok[] = {0U};
    const char *const candidates[] = {"b.c", "z.txt", "A.c"};
    const char *const globbed[] = {"A.c", "b.c"};

    CHECK(start_session(&session));
    CHECK(expect_subject(&session, WSH_RUNTIME_LAUNCH, "probe",
        NULL, 0U, ok, 1U) == WSH_OK);
    CHECK(expect_subject(&session, WSH_RUNTIME_MATCH_PATHS, "*.c",
        candidates, 3U, ok, 1U) == WSH_OK);
    CHECK(expect_call(&session, WSH_RUNTIME_LAUNCH, "probe",
        globbed, 2U, NULL, 0U, ok, 1U, WSH_OK) == WSH_OK);
    status = NULL;
    CHECK(evaluate_text(session.evaluator,
        "x=(one '' three); probe $x $#x $\"x $x(2); probe *.c",
        &status) == WSH_OK);
    CHECK(status_equals(status, ok, 1U));
    CHECK(wsh_fake_runtime_complete(session.fake) == WSH_OK);
    wsh_status_list_destroy(status);
    stop_session(&session);
    return 1;
}

/** Verify caret pairing, distribution, and invalid cardinality. */
static int test_tc_0009(void)
{
    test_session session;
    wsh_status_list *status;
    const uint32_t ok[] = {0U};
    const char *const expected[] = {"x1", "y2", "prex", "prey"};

    CHECK(start_session(&session));
    CHECK(expect_call(&session, WSH_RUNTIME_LAUNCH, "probe",
        expected, 4U, NULL, 0U, ok, 1U, WSH_OK) == WSH_OK);
    status = NULL;
    CHECK(evaluate_text(session.evaluator,
        "a=(x y); b=(1 2); probe $a^$b pre^$a", &status) == WSH_OK);
    CHECK(wsh_fake_runtime_complete(session.fake) == WSH_OK);
    wsh_status_list_destroy(status);
    CHECK(evaluate_text(session.evaluator,
        "a=(x y); b=(1 2 3); probe $a^$b", &status) == WSH_ERR_MISMATCH);
    CHECK(wsh_fake_runtime_call_count(session.fake) == 1U);
    stop_session(&session);
    return 1;
}

/** Verify all-zero conditional short-circuit composition. */
static int test_tc_0014(void)
{
    test_session session;
    wsh_status_list *status;
    const uint32_t fail[] = {1U};
    const uint32_t ok[] = {0U};

    CHECK(start_session(&session));
    CHECK(expect_call(&session, WSH_RUNTIME_LAUNCH, "fail",
        NULL, 0U, NULL, 0U, fail, 1U, WSH_OK) == WSH_OK);
    CHECK(expect_call(&session, WSH_RUNTIME_LAUNCH, "ok",
        NULL, 0U, NULL, 0U, ok, 1U, WSH_OK) == WSH_OK);
    status = NULL;
    CHECK(evaluate_text(session.evaluator,
        "fail && skipped || ok", &status) == WSH_OK);
    CHECK(status_equals(status, ok, 1U));
    CHECK(wsh_fake_runtime_complete(session.fake) == WSH_OK);
    wsh_status_list_destroy(status);
    stop_session(&session);
    return 1;
}

/** Verify functions, branches, loops, switch, and return. */
static int test_tc_0017(void)
{
    test_session session;
    wsh_status_list *status;
    const uint32_t ok[] = {0U};
    const char *const ab[] = {"a", "b"};
    const char *const p[] = {"p"};
    const char *const q[] = {"q"};

    CHECK(start_session(&session));
    CHECK(expect_call(&session, WSH_RUNTIME_LAUNCH, "probe",
        ab, 2U, NULL, 0U, ok, 1U, WSH_OK) == WSH_OK);
    CHECK(expect_call(&session, WSH_RUNTIME_LAUNCH, "yescmd",
        NULL, 0U, NULL, 0U, ok, 1U, WSH_OK) == WSH_OK);
    CHECK(expect_call(&session, WSH_RUNTIME_LAUNCH, "loop",
        p, 1U, NULL, 0U, ok, 1U, WSH_OK) == WSH_OK);
    CHECK(expect_call(&session, WSH_RUNTIME_LAUNCH, "loop",
        q, 1U, NULL, 0U, ok, 1U, WSH_OK) == WSH_OK);
    CHECK(expect_call(&session, WSH_RUNTIME_LAUNCH, "right",
        NULL, 0U, NULL, 0U, ok, 1U, WSH_OK) == WSH_OK);
    status = NULL;
    CHECK(evaluate_text(session.evaluator,
        "fn f { probe $*; return 7 }; f a b; "
        "if (~ yes y*) { yescmd }; "
        "for (x in p q) { loop $x }; "
        "switch (q) { case p; wrong; case q; right }",
        &status) == WSH_OK);
    CHECK(wsh_fake_runtime_complete(session.fake) == WSH_OK);
    wsh_status_list_destroy(status);
    stop_session(&session);
    return 1;
}

/** Verify source-named semantic diagnostics with spans. */
static int test_tc_0019(void)
{
    test_session session;
    wsh_status_list *status;
    wsh_diagnostic_view diagnostic;

    CHECK(start_session(&session));
    status = NULL;
    CHECK(evaluate_text(session.evaluator,
        "a=(x y); b=(1 2 3); probe $a^$b", &status) == WSH_ERR_MISMATCH);
    CHECK(wsh_context_diagnostic_count(session.context) >= 1U);
    CHECK(wsh_context_diagnostic_at(
        session.context, 0U, &diagnostic) == WSH_OK);
    CHECK(diagnostic.severity == WSH_DIAGNOSTIC_ERROR);
    CHECK(diagnostic.code == WSH_DIAGNOSTIC_EXPANSION);
    CHECK(diagnostic.has_span);
    CHECK(wsh_string_view_equal(diagnostic.source_name,
        wsh_string_view_from_cstr("m4-test.wsh")));
    stop_session(&session);
    return 1;
}

/** Verify failed substitution suppresses the containing launch. */
static int test_tc_0023(void)
{
    test_session session;
    wsh_status_list *status;
    const uint32_t fail[] = {9U};
    const char *const output[] = {"should not escape"};

    CHECK(start_session(&session));
    CHECK(expect_call(&session, WSH_RUNTIME_LAUNCH, "inner",
        NULL, 0U, output, 1U, fail, 1U, WSH_OK) == WSH_OK);
    status = NULL;
    CHECK(evaluate_text(session.evaluator,
        "outer `{inner}", &status) == WSH_ERR_MISMATCH);
    CHECK(wsh_fake_runtime_call_count(session.fake) == 1U);
    CHECK(wsh_fake_runtime_complete(session.fake) == WSH_OK);
    stop_session(&session);
    return 1;
}

/** Sweep evaluator allocations for leak-free failure cleanup. */
static int test_tc_0024(void)
{
    size_t ordinal;
    fault_allocator state;
    wsh_allocator allocator;
    wsh_context_options context_options;
    wsh_evaluator_options evaluator_options;
    wsh_context *context;
    wsh_evaluator *evaluator;
    wsh_source *source;
    wsh_parse_tree *tree;
    wsh_parser_options parser_options;
    wsh_status_list *status;
    wsh_result result;
    size_t base_calls;

    for (ordinal = 1U; ordinal < 300U; ++ordinal) {
        memset(&state, 0, sizeof(state));
        allocator.user_data = &state;
        allocator.allocate = fault_allocate;
        allocator.deallocate = fault_deallocate;
        wsh_context_options_init(&context_options);
        context_options.allocator = allocator;
        CHECK(wsh_context_create(&context_options, &context) == WSH_OK);
        wsh_evaluator_options_init(&evaluator_options);
        evaluator_options.allocator = allocator;
        CHECK(wsh_evaluator_create(
            context, &evaluator_options, &evaluator) == WSH_OK);
        CHECK(wsh_source_create(&allocator, NULL,
            (const unsigned char *)"fn f { x=(a b) }; f",
            strlen("fn f { x=(a b) }; f"), &source) == WSH_OK);
        wsh_parser_options_init(&parser_options);
        parser_options.allocator = allocator;
        CHECK(wsh_parse(&parser_options, source, &tree) == WSH_OK);
        base_calls = state.calls;
        state.fail_at = base_calls + ordinal;
        status = NULL;
        result = wsh_evaluate(evaluator, tree, &status);
        wsh_status_list_destroy(status);
        wsh_parse_tree_destroy(tree);
        wsh_source_destroy(source);
        wsh_evaluator_destroy(evaluator);
        wsh_context_destroy(context);
        CHECK(state.outstanding == 0U);
        if (result == WSH_OK) {
            break;
        }
    }
    CHECK(ordinal < 300U);
    return 1;
}

/** Verify supplementary Unicode values cross the runtime boundary. */
static int test_tc_0037(void)
{
    test_session session;
    wsh_status_list *status;
    const uint32_t ok[] = {0U};
    const char *const expected[] = {"\xF0\x9F\x98\x80", "\xF0\x9F\x98\x80"};

    CHECK(start_session(&session));
    CHECK(expect_call(&session, WSH_RUNTIME_LAUNCH, "probe",
        expected, 2U, NULL, 0U, ok, 1U, WSH_OK) == WSH_OK);
    status = NULL;
    CHECK(evaluate_text(session.evaluator,
        "emoji='\xF0\x9F\x98\x80'; probe '\xF0\x9F\x98\x80' $emoji",
        &status) == WSH_OK);
    CHECK(wsh_fake_runtime_complete(session.fake) == WSH_OK);
    wsh_status_list_destroy(status);
    stop_session(&session);
    return 1;
}

/** Verify source/eval caller state and explicit WSH launch. */
static int test_tc_0043(void)
{
    test_session session;
    wsh_status_list *status;
    const uint32_t ok[] = {0U};
    const char *const source_args[] = {"arg"};
    const char *const source_output[] = {"x=fromsource; local y=inside"};
    const wsh_value *value;
    const char *const x_expected[] = {"fromsource"};
    const char *const child_args[] = {"arg"};

    CHECK(start_session(&session));
    CHECK(expect_call(&session, WSH_RUNTIME_READ_SOURCE, "lib.wsh",
        source_args, 1U, source_output, 1U, ok, 1U, WSH_OK) == WSH_OK);
    CHECK(expect_call(&session, WSH_RUNTIME_LAUNCH, "child.wsh",
        child_args, 1U, NULL, 0U, ok, 1U, WSH_OK) == WSH_OK);
    status = NULL;
    CHECK(evaluate_text(session.evaluator,
        "source lib.wsh arg; eval 'z=fromeval'; child.wsh arg",
        &status) == WSH_OK);
    CHECK(wsh_context_get_variable(session.context,
        wsh_string_view_from_cstr("x"), &value) == WSH_OK);
    CHECK(value_equals(value, x_expected, 1U));
    CHECK(wsh_context_get_variable(session.context,
        wsh_string_view_from_cstr("y"), &value) == WSH_ERR_INVALID);
    CHECK(wsh_context_get_variable(session.context,
        wsh_string_view_from_cstr("z"), &value) == WSH_OK);
    CHECK(wsh_fake_runtime_complete(session.fake) == WSH_OK);
    wsh_status_list_destroy(status);
    stop_session(&session);
    return 1;
}

/** Verify local, command-local, persistent, and exported state. */
static int test_tc_0046(void)
{
    test_session session;
    wsh_status_list *status;
    const uint32_t ok[] = {0U};
    const char *const inner[] = {"inner"};
    const char *const outer[] = {"outer"};
    const char *const temporary[] = {"value"};
    int exported;

    CHECK(start_session(&session));
    CHECK(expect_call(&session, WSH_RUNTIME_LAUNCH, "check",
        inner, 1U, NULL, 0U, ok, 1U, WSH_OK) == WSH_OK);
    CHECK(expect_call(&session, WSH_RUNTIME_LAUNCH, "check",
        outer, 1U, NULL, 0U, ok, 1U, WSH_OK) == WSH_OK);
    CHECK(expect_call(&session, WSH_RUNTIME_LAUNCH, "check",
        temporary, 1U, NULL, 0U, ok, 1U, WSH_OK) == WSH_OK);
    CHECK(expect_call(&session, WSH_RUNTIME_LAUNCH, "check",
        NULL, 0U, NULL, 0U, ok, 1U, WSH_OK) == WSH_OK);
    status = NULL;
    CHECK(evaluate_text(session.evaluator,
        "x=outer; { local x=inner; check $x }; check $x; "
        "fn show { check $tmp }; tmp=value show; check $tmp; export x",
        &status) == WSH_OK);
    CHECK(wsh_context_is_exported(session.context,
        wsh_string_view_from_cstr("x"), &exported) == WSH_OK);
    CHECK(exported == 1);
    CHECK(wsh_fake_runtime_complete(session.fake) == WSH_OK);
    wsh_status_list_destroy(status);
    stop_session(&session);
    return 1;
}

/** Verify private identity and exported-name collision handling. */
static int test_tc_0048(void)
{
    test_session session;
    wsh_status_list *status;
    int exported;

    CHECK(start_session(&session));
    status = NULL;
    CHECK(evaluate_text(session.evaluator,
        "name=a; NAME=b; export name", &status) == WSH_OK);
    wsh_status_list_destroy(status);
    status = NULL;
    CHECK(evaluate_text(session.evaluator,
        "export NAME", &status) == WSH_ERR_MISMATCH);
    CHECK(wsh_context_is_exported(session.context,
        wsh_string_view_from_cstr("name"), &exported) == WSH_OK && exported);
    CHECK(wsh_context_is_exported(session.context,
        wsh_string_view_from_cstr("NAME"), &exported) == WSH_OK && !exported);
    stop_session(&session);
    return 1;
}

/** Verify unsigned multi-element status propagation. */
static int test_tc_0049(void)
{
    test_session session;
    wsh_status_list *status;
    const uint32_t codes[] = {0U, UINT32_MAX};
    const wsh_value *value;
    const char *const expected[] = {"0", "4294967295"};

    CHECK(start_session(&session));
    CHECK(expect_call(&session, WSH_RUNTIME_LAUNCH, "multi",
        NULL, 0U, NULL, 0U, codes, 2U, WSH_OK) == WSH_OK);
    status = NULL;
    CHECK(evaluate_text(session.evaluator, "multi", &status) == WSH_OK);
    CHECK(status_equals(status, codes, 2U));
    CHECK(!wsh_status_list_is_success(status));
    CHECK(wsh_context_get_variable(session.context,
        wsh_string_view_from_cstr("status"), &value) == WSH_OK);
    CHECK(value_equals(value, expected, 2U));
    wsh_status_list_destroy(status);
    stop_session(&session);
    return 1;
}

/** Verify successful command capture and IFS splitting. */
static int test_tc_0052(void)
{
    test_session session;
    wsh_status_list *status;
    const uint32_t ok[] = {0U};
    const char *const output[] = {"a b\nc"};
    const char *const expected[] = {"a", "b", "c"};

    CHECK(start_session(&session));
    CHECK(expect_call(&session, WSH_RUNTIME_LAUNCH, "inner",
        NULL, 0U, output, 1U, ok, 1U, WSH_OK) == WSH_OK);
    CHECK(expect_call(&session, WSH_RUNTIME_LAUNCH, "outer",
        expected, 3U, NULL, 0U, ok, 1U, WSH_OK) == WSH_OK);
    status = NULL;
    CHECK(evaluate_text(session.evaluator,
        "outer `{inner}", &status) == WSH_OK);
    CHECK(wsh_fake_runtime_complete(session.fake) == WSH_OK);
    wsh_status_list_destroy(status);
    stop_session(&session);
    return 1;
}

/** Verify hostile infinite evaluation reaches a finite ceiling. */
static int test_tc_0074(void)
{
    test_session session;
    wsh_evaluator_options options;
    wsh_status_list *status;

    memset(&session, 0, sizeof(session));
    CHECK(wsh_fake_runtime_create(NULL, NULL, &session.fake) == WSH_OK);
    {
        wsh_context_options context_options;
        wsh_context_options_init(&context_options);
        context_options.runtime = wsh_fake_runtime_interface(session.fake);
        CHECK(wsh_context_create(
            &context_options, &session.context) == WSH_OK);
    }
    wsh_evaluator_options_init(&options);
    options.max_steps = 40U;
    CHECK(wsh_evaluator_create(
        session.context, &options, &session.evaluator) == WSH_OK);
    status = NULL;
    CHECK(evaluate_text(session.evaluator,
        "while (~ x x) { }", &status) == WSH_ERR_RESOURCE);
    CHECK(wsh_context_diagnostic_count(session.context) != 0U);
    stop_session(&session);
    return 1;
}

/** Verify subshell isolation and pre-effect M5 rejection. */
static int test_tc_0075(void)
{
    test_session session;
    wsh_status_list *status;
    const uint32_t ok[] = {0U};
    const char *const expected[] = {"outer"};

    CHECK(start_session(&session));
    CHECK(expect_call(&session, WSH_RUNTIME_LAUNCH, "probe",
        expected, 1U, NULL, 0U, ok, 1U, WSH_OK) == WSH_OK);
    status = NULL;
    CHECK(evaluate_text(session.evaluator,
        "x=outer; @ { x=inner }; probe $x", &status) == WSH_OK);
    CHECK(wsh_fake_runtime_complete(session.fake) == WSH_OK);
    wsh_status_list_destroy(status);
    CHECK(evaluate_text(session.evaluator,
        "probe >file", &status) == WSH_ERR_INVALID);
    CHECK(wsh_fake_runtime_call_count(session.fake) == 1U);
    stop_session(&session);
    return 1;
}

/** Signature shared by controlled test functions. */
typedef int (*test_function)(void);

/** Stable controlled identifier and implementation pair. */
typedef struct test_entry {
    /** Stable TC-NNNN string. */
    const char *id;
    /** Test implementation. */
    test_function function;
} test_entry;

/** Dispatch one controlled family or the complete M4 unit suite. */
int main(int argc, char **argv)
{
    static const test_entry tests[] = {
        {"TC-0007", test_tc_0007},
        {"TC-0009", test_tc_0009},
        {"TC-0014", test_tc_0014},
        {"TC-0017", test_tc_0017},
        {"TC-0019", test_tc_0019},
        {"TC-0023", test_tc_0023},
        {"TC-0024", test_tc_0024},
        {"TC-0037", test_tc_0037},
        {"TC-0043", test_tc_0043},
        {"TC-0046", test_tc_0046},
        {"TC-0048", test_tc_0048},
        {"TC-0049", test_tc_0049},
        {"TC-0052", test_tc_0052},
        {"TC-0074", test_tc_0074},
        {"TC-0075", test_tc_0075}
    };
    size_t index;
    int passed;

    if (argc == 2) {
        for (index = 0U; index < sizeof(tests) / sizeof(tests[0]); ++index) {
            if (strcmp(argv[1], tests[index].id) == 0) {
                passed = tests[index].function();
                printf("%s: %s\n", tests[index].id,
                    passed ? "PASS" : "FAIL");
                return passed ? 0 : 1;
            }
        }
        fprintf(stderr, "unknown test case: %s\n", argv[1]);
        return 2;
    }
    passed = 1;
    for (index = 0U; index < sizeof(tests) / sizeof(tests[0]); ++index) {
        int current = tests[index].function();
        printf("%s: %s\n", tests[index].id,
            current ? "PASS" : "FAIL");
        if (!current) {
            passed = 0;
        }
    }
    return passed ? 0 : 1;
}
