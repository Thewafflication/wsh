/**
 * @file portable_core_tests.c
 * @brief Controlled M2 portable-core verification cases.
 */

#include "wsh/core.h"

#include <limits.h>
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#include <windows.h>
#endif

/** Print one objective failed expression and leave the case immediately. */
#define CHECK(expression) \
    do { \
        if (!(expression)) { \
            fprintf( \
                stderr, \
                "CHECK failed at %s:%d: %s\n", \
                __FILE__, \
                __LINE__, \
                #expression); \
            return 0; \
        } \
    } while (0)

/** Allocation state used for deterministic fault and leak testing. */
typedef struct tracking_allocator {
    /** Number of allocation callbacks observed. */
    size_t calls;
    /** One-based callback ordinal to reject, or zero. */
    size_t fail_at;
    /** Number of successfully allocated blocks not yet released. */
    size_t outstanding;
} tracking_allocator;

/** One test case table entry. */
typedef struct test_case_entry {
    /** Stable TC-NNNN identifier. */
    const char *identifier;
    /** Test implementation returning nonzero only on Pass. */
    int (*function)(void);
} test_case_entry;

/** Allocate one tracked block unless its ordinal is selected to fail. */
static void *tracking_allocate(void *user_data, size_t size)
{
    tracking_allocator *tracker = (tracking_allocator *)user_data;
    void *pointer;

    tracker->calls += 1U;
    if (tracker->fail_at != 0U && tracker->calls == tracker->fail_at) {
        return NULL;
    }
    pointer = malloc(size == 0U ? 1U : size);
    if (pointer != NULL) {
        tracker->outstanding += 1U;
    }
    return pointer;
}

/** Release one tracked block and update the leak count. */
static void tracking_deallocate(void *user_data, void *pointer)
{
    tracking_allocator *tracker = (tracking_allocator *)user_data;

    if (pointer != NULL) {
        if (tracker->outstanding == 0U) {
            fprintf(stderr, "tracking allocator underflow\n");
            abort();
        }
        tracker->outstanding -= 1U;
        free(pointer);
    }
}

/** Return allocator callbacks bound to one tracking state. */
static wsh_allocator tracked_allocator(tracking_allocator *tracker)
{
    wsh_allocator allocator;

    allocator.user_data = tracker;
    allocator.allocate = tracking_allocate;
    allocator.deallocate = tracking_deallocate;
    return allocator;
}

/** Build an immutable value from an array of C strings. */
static wsh_result make_value(
    const wsh_allocator *allocator,
    const wsh_limits *limits,
    const char *const *items,
    size_t count,
    wsh_value **out_value)
{
    wsh_value_builder *builder;
    size_t index;
    wsh_result result;

    if (out_value == NULL) {
        return WSH_ERR_INVALID;
    }
    *out_value = NULL;
    result = wsh_value_builder_create(allocator, limits, &builder);
    if (result != WSH_OK) {
        return result;
    }
    for (index = 0U; index < count; ++index) {
        result = wsh_value_builder_append(
            builder,
            wsh_string_view_from_cstr(items[index]));
        if (result != WSH_OK) {
            wsh_value_builder_destroy(builder);
            return result;
        }
    }
    result = wsh_value_builder_finish(builder, out_value);
    wsh_value_builder_destroy(builder);
    return result;
}

/** Build an immutable status list from an array. */
static wsh_result make_status(
    const wsh_allocator *allocator,
    const wsh_limits *limits,
    const uint32_t *items,
    size_t count,
    wsh_status_list **out_status)
{
    wsh_status_builder *builder;
    size_t index;
    wsh_result result;

    if (out_status == NULL) {
        return WSH_ERR_INVALID;
    }
    *out_status = NULL;
    result = wsh_status_builder_create(allocator, limits, &builder);
    if (result != WSH_OK) {
        return result;
    }
    for (index = 0U; index < count; ++index) {
        result = wsh_status_builder_append(builder, items[index]);
        if (result != WSH_OK) {
            wsh_status_builder_destroy(builder);
            return result;
        }
    }
    result = wsh_status_builder_finish(builder, out_status);
    wsh_status_builder_destroy(builder);
    return result;
}

/** Return whether a view equals a C string. */
static int view_equals_cstr(wsh_string_view view, const char *expected)
{
    return wsh_string_view_equal(
        view,
        wsh_string_view_from_cstr(expected));
}

/** Return whether one hostile byte sequence receives the encoding result. */
static int invalid_utf8_is_rejected(
    const unsigned char *bytes,
    size_t length)
{
    wsh_string_view view;

    view.data = (const char *)bytes;
    view.length = length;
    return wsh_utf8_validate(view, NULL) == WSH_ERR_ENCODING;
}

/** Verify immutable ordered flat Unicode values. */
static int test_tc_0007(void)
{
    char mutable_text[] = "alpha";
    wsh_value_builder *builder = NULL;
    wsh_value *value = NULL;
    wsh_value *clone = NULL;
    wsh_string_view element;

    CHECK(wsh_value_builder_create(NULL, NULL, &builder) == WSH_OK);
    CHECK(wsh_value_builder_append(
        builder,
        wsh_string_view_from_cstr("")) == WSH_OK);
    CHECK(wsh_value_builder_append(
        builder,
        wsh_string_view_from_cstr(mutable_text)) == WSH_OK);
    mutable_text[0] = 'X';
    CHECK(wsh_value_builder_append(
        builder,
        wsh_string_view_from_cstr("\xF0\x9F\x98\x80")) == WSH_OK);
    CHECK(wsh_value_builder_finish(builder, &value) == WSH_OK);
    CHECK(wsh_value_count(value) == 3U);
    CHECK(wsh_value_at(value, 0U, &element) == WSH_OK);
    CHECK(view_equals_cstr(element, ""));
    CHECK(wsh_value_at(value, 1U, &element) == WSH_OK);
    CHECK(view_equals_cstr(element, "alpha"));
    CHECK(wsh_value_at(value, 2U, &element) == WSH_OK);
    CHECK(view_equals_cstr(element, "\xF0\x9F\x98\x80"));
    CHECK(wsh_value_at(value, 3U, &element) == WSH_ERR_INVALID);
    CHECK(wsh_value_clone(NULL, NULL, value, &clone) == WSH_OK);
    wsh_value_destroy(value);
    value = NULL;
    CHECK(wsh_value_at(clone, 1U, &element) == WSH_OK);
    CHECK(view_equals_cstr(element, "alpha"));

    wsh_value_destroy(clone);
    wsh_value_builder_destroy(builder);
    return 1;
}

/** Verify strict UTF validation and portable UTF-16 conversion. */
static int test_tc_0018(void)
{
    static const char valid[] =
        "A\xC3\xA9\xE2\x82\xAC\xF0\x9F\x98\x80";
    static const unsigned char overlong[] = {0xC0U, 0xAFU};
    static const unsigned char truncated[] = {0xE2U, 0x82U};
    static const unsigned char surrogate[] = {0xEDU, 0xA0U, 0x80U};
    static const unsigned char too_large[] = {0xF4U, 0x90U, 0x80U, 0x80U};
    static const unsigned char nul[] = {0x00U};
    static const unsigned char noncharacter[] = {0xEFU, 0xB7U, 0x90U};
    uint16_t *units = NULL;
    size_t unit_count = 0U;
    char *round_trip = NULL;
    size_t byte_count = 0U;
    size_t scalar_count = 0U;
    wsh_string_view view;

    view = wsh_string_view_from_cstr(valid);
    CHECK(wsh_utf8_validate(view, &scalar_count) == WSH_OK);
    CHECK(scalar_count == 4U);
    CHECK(wsh_utf8_to_utf16(
        NULL,
        NULL,
        view,
        &units,
        &unit_count) == WSH_OK);
    CHECK(unit_count == 5U);
    CHECK(units[3] == 0xD83DU && units[4] == 0xDE00U);
    CHECK(units[5] == 0U);
    CHECK(wsh_utf16_to_utf8(
        NULL,
        NULL,
        units,
        unit_count,
        &round_trip,
        &byte_count) == WSH_OK);
    CHECK(byte_count == view.length);
    CHECK(memcmp(round_trip, view.data, view.length) == 0);
    CHECK(round_trip[byte_count] == '\0');
    wsh_allocator_release(NULL, round_trip);
    wsh_allocator_release(NULL, units);

    CHECK(invalid_utf8_is_rejected(overlong, sizeof(overlong)));
    CHECK(invalid_utf8_is_rejected(truncated, sizeof(truncated)));
    CHECK(invalid_utf8_is_rejected(surrogate, sizeof(surrogate)));
    CHECK(invalid_utf8_is_rejected(too_large, sizeof(too_large)));
    CHECK(invalid_utf8_is_rejected(nul, sizeof(nul)));
    CHECK(invalid_utf8_is_rejected(
        noncharacter,
        sizeof(noncharacter)));
    return 1;
}

/** Verify structured diagnostic location and bounded retention. */
static int test_tc_0019(void)
{
    static const unsigned char source_bytes[] = "a\tb\nz";
    wsh_limits limits = wsh_limits_default();
    wsh_context_options options;
    wsh_source *source = NULL;
    wsh_context *context = NULL;
    wsh_source_span span;
    wsh_diagnostic_view diagnostic;

    limits.max_diagnostics = 1U;
    wsh_context_options_init(&options);
    options.limits = limits;
    CHECK(wsh_source_create(
        NULL,
        &limits,
        source_bytes,
        sizeof(source_bytes) - 1U,
        &source) == WSH_OK);
    CHECK(wsh_source_get_span(source, 2U, 1U, &span) == WSH_OK);
    CHECK(span.start.original_byte_offset == 2U);
    CHECK(span.start.scalar_offset == 2U);
    CHECK(span.start.line == 1U);
    CHECK(span.start.scalar_column == 3U);
    CHECK(span.start.display_column == 9U);
    CHECK(wsh_context_create(&options, &context) == WSH_OK);
    CHECK(wsh_context_add_diagnostic(
        context,
        WSH_DIAGNOSTIC_ERROR,
        WSH_DIAGNOSTIC_INVALID_ENCODING,
        wsh_string_view_from_cstr("invalid input"),
        wsh_string_view_from_cstr("sample.wsh"),
        &span) == WSH_OK);
    CHECK(wsh_context_add_diagnostic(
        context,
        WSH_DIAGNOSTIC_NOTE,
        WSH_DIAGNOSTIC_LIMIT,
        wsh_string_view_from_cstr("second"),
        wsh_string_view_from_cstr(""),
        NULL) == WSH_ERR_RESOURCE);
    CHECK(wsh_context_diagnostic_count(context) == 1U);
    CHECK(wsh_context_diagnostic_at(context, 0U, &diagnostic) == WSH_OK);
    CHECK(diagnostic.severity == WSH_DIAGNOSTIC_ERROR);
    CHECK(diagnostic.code == WSH_DIAGNOSTIC_INVALID_ENCODING);
    CHECK(view_equals_cstr(diagnostic.message, "invalid input"));
    CHECK(view_equals_cstr(diagnostic.source_name, "sample.wsh"));
    CHECK(diagnostic.has_span != 0);
    CHECK(diagnostic.span.start.display_column == 9U);

    wsh_context_destroy(context);
    wsh_source_destroy(source);
    return 1;
}

/**
 * Exercise a representative full ownership graph with one fault ordinal.
 * The function always cleans up every output that was committed.
 */
static int exercise_fault_graph(
    tracking_allocator *tracker,
    int *out_completed)
{
    static const unsigned char source_bytes[] = "one\r\ntwo";
    static const char *const value_items[] = {"one", "two"};
    static const uint32_t statuses[] = {0U, 5U};
    wsh_allocator allocator = tracked_allocator(tracker);
    wsh_limits limits = wsh_limits_default();
    wsh_context_options options;
    wsh_fake_runtime *fake = NULL;
    wsh_context *context = NULL;
    wsh_value *value = NULL;
    wsh_status_list *status = NULL;
    wsh_source *source = NULL;
    wsh_result result;

    *out_completed = 0;
    result = wsh_fake_runtime_create(&allocator, &limits, &fake);
    if (result != WSH_OK) {
        goto cleanup;
    }
    wsh_context_options_init(&options);
    options.allocator = allocator;
    options.limits = limits;
    options.runtime = wsh_fake_runtime_interface(fake);
    result = wsh_context_create(&options, &context);
    if (result != WSH_OK) {
        goto cleanup;
    }
    result = make_value(
        &allocator,
        &limits,
        value_items,
        2U,
        &value);
    if (result != WSH_OK) {
        goto cleanup;
    }
    result = make_status(
        &allocator,
        &limits,
        statuses,
        2U,
        &status);
    if (result != WSH_OK) {
        goto cleanup;
    }
    result = wsh_source_create(
        &allocator,
        &limits,
        source_bytes,
        sizeof(source_bytes) - 1U,
        &source);
    if (result != WSH_OK) {
        goto cleanup;
    }
    result = wsh_context_set_variable(
        context,
        wsh_string_view_from_cstr("name"),
        value);
    if (result != WSH_OK) {
        goto cleanup;
    }
    result = wsh_context_add_diagnostic(
        context,
        WSH_DIAGNOSTIC_WARNING,
        WSH_DIAGNOSTIC_LIMIT,
        wsh_string_view_from_cstr("bounded"),
        wsh_string_view_from_cstr("source"),
        NULL);
    if (result != WSH_OK) {
        goto cleanup;
    }
    result = wsh_fake_runtime_expect(
        fake,
        WSH_RUNTIME_LAUNCH,
        wsh_string_view_from_cstr("tool"),
        value,
        status,
        WSH_OK);
    if (result != WSH_OK) {
        goto cleanup;
    }
    *out_completed = 1;

cleanup:
    wsh_source_destroy(source);
    wsh_status_list_destroy(status);
    wsh_value_destroy(value);
    wsh_context_destroy(context);
    wsh_fake_runtime_destroy(fake);
    return tracker->outstanding == 0U;
}

/** Verify allocation failures are fault atomic and leak free. */
static int test_tc_0024(void)
{
    tracking_allocator tracker;
    size_t fail_at;
    int completed = 0;
    wsh_allocator allocator;
    wsh_limits limits = wsh_limits_default();
    wsh_string_builder *builder = NULL;
    wsh_string *string = NULL;

    for (fail_at = 1U; fail_at < 256U; ++fail_at) {
        memset(&tracker, 0, sizeof(tracker));
        tracker.fail_at = fail_at;
        CHECK(exercise_fault_graph(&tracker, &completed));
        if (completed) {
            break;
        }
    }
    CHECK(completed);
    CHECK(fail_at > 10U);

    memset(&tracker, 0, sizeof(tracker));
    allocator = tracked_allocator(&tracker);
    CHECK(wsh_string_builder_create(
        &allocator,
        &limits,
        &builder) == WSH_OK);
    CHECK(wsh_string_builder_append(
        builder,
        wsh_string_view_from_cstr("a")) == WSH_OK);
    tracker.fail_at = tracker.calls + 1U;
    CHECK(wsh_string_builder_append(
        builder,
        wsh_string_view_from_cstr("longer")) == WSH_ERR_RESOURCE);
    tracker.fail_at = 0U;
    CHECK(wsh_string_builder_finish(builder, &string) == WSH_OK);
    CHECK(view_equals_cstr(wsh_string_bytes(string), "a"));
    wsh_string_destroy(string);
    wsh_string_builder_destroy(builder);
    CHECK(tracker.outstanding == 0U);
    return 1;
}

/** Verify all specified source line endings and final-line behavior. */
static int test_tc_0034(void)
{
    static const unsigned char mixed[] = "a\r\nb\nc\rd";
    static const unsigned char terminated[] = "x\n";
    static const char normalized[] = "a\nb\nc\nd";
    wsh_source *source = NULL;
    wsh_source *empty = NULL;
    wsh_source *ending = NULL;
    wsh_string_view text;
    wsh_source_span span;

    CHECK(wsh_source_create(
        NULL,
        NULL,
        mixed,
        sizeof(mixed) - 1U,
        &source) == WSH_OK);
    text = wsh_source_text(source);
    CHECK(text.length == sizeof(normalized) - 1U);
    CHECK(memcmp(text.data, normalized, text.length) == 0);
    CHECK(wsh_source_line_count(source) == 4U);
    CHECK(wsh_source_get_span(source, 2U, 1U, &span) == WSH_OK);
    CHECK(span.start.original_byte_offset == 3U);
    CHECK(span.start.line == 2U && span.start.scalar_column == 1U);
    CHECK(wsh_source_get_span(source, 6U, 1U, &span) == WSH_OK);
    CHECK(span.start.original_byte_offset == 7U);
    CHECK(span.start.line == 4U && span.start.scalar_column == 1U);
    CHECK(wsh_source_create(NULL, NULL, NULL, 0U, &empty) == WSH_OK);
    CHECK(wsh_source_line_count(empty) == 1U);
    CHECK(wsh_source_create(
        NULL,
        NULL,
        terminated,
        sizeof(terminated) - 1U,
        &ending) == WSH_OK);
    CHECK(wsh_source_line_count(ending) == 2U);

    wsh_source_destroy(ending);
    wsh_source_destroy(empty);
    wsh_source_destroy(source);
    return 1;
}

/** Verify canonical UTF-8 source and leading-only BOM handling. */
static int test_tc_0035(void)
{
    static const unsigned char plain[] = "alpha";
    static const unsigned char with_bom[] = {
        0xEFU, 0xBBU, 0xBFU, 'a', 'l', 'p', 'h', 'a'
    };
    static const unsigned char embedded_bom[] = {
        'a', 0xEFU, 0xBBU, 0xBFU, 'b'
    };
    static const unsigned char nul_source[] = {'a', 0U, 'b'};
    static const unsigned char utf16_looking[] = {'A', 0U};
    wsh_source *source = NULL;
    wsh_string_view text;

    CHECK(wsh_source_create(
        NULL,
        NULL,
        plain,
        sizeof(plain) - 1U,
        &source) == WSH_OK);
    CHECK(wsh_source_bom(source) == WSH_BOM_NONE);
    CHECK(view_equals_cstr(wsh_source_text(source), "alpha"));
    wsh_source_destroy(source);
    source = NULL;
    CHECK(wsh_source_create(
        NULL,
        NULL,
        with_bom,
        sizeof(with_bom),
        &source) == WSH_OK);
    CHECK(wsh_source_bom(source) == WSH_BOM_UTF8);
    CHECK(view_equals_cstr(wsh_source_text(source), "alpha"));
    wsh_source_destroy(source);
    source = NULL;
    CHECK(wsh_source_create(
        NULL,
        NULL,
        embedded_bom,
        sizeof(embedded_bom),
        &source) == WSH_OK);
    text = wsh_source_text(source);
    CHECK(text.length == sizeof(embedded_bom));
    CHECK(memcmp(text.data, embedded_bom, sizeof(embedded_bom)) == 0);
    wsh_source_destroy(source);
    source = NULL;
    CHECK(wsh_source_create(
        NULL,
        NULL,
        nul_source,
        sizeof(nul_source),
        &source) == WSH_ERR_ENCODING);
    CHECK(source == NULL);
    CHECK(wsh_source_create(
        NULL,
        NULL,
        utf16_looking,
        sizeof(utf16_looking),
        &source) == WSH_ERR_ENCODING);
    CHECK(source == NULL);
    return 1;
}

/** Verify BOM-marked UTF-16LE and UTF-16BE source equivalence. */
static int test_tc_0036(void)
{
    static const unsigned char little_endian[] = {
        0xFFU, 0xFEU, 0x41U, 0x00U,
        0x3DU, 0xD8U, 0x00U, 0xDEU,
        0x0DU, 0x00U, 0x0AU, 0x00U,
        0x42U, 0x00U
    };
    static const unsigned char big_endian[] = {
        0xFEU, 0xFFU, 0x00U, 0x41U,
        0xD8U, 0x3DU, 0xDEU, 0x00U,
        0x00U, 0x0DU, 0x00U, 0x0AU,
        0x00U, 0x42U
    };
    static const unsigned char unpaired[] = {
        0xFFU, 0xFEU, 0x00U, 0xD8U
    };
    static const unsigned char odd[] = {
        0xFFU, 0xFEU, 0x41U
    };
    static const char expected[] = "A\xF0\x9F\x98\x80\nB";
    wsh_source *little = NULL;
    wsh_source *big = NULL;

    CHECK(wsh_source_create(
        NULL,
        NULL,
        little_endian,
        sizeof(little_endian),
        &little) == WSH_OK);
    CHECK(wsh_source_create(
        NULL,
        NULL,
        big_endian,
        sizeof(big_endian),
        &big) == WSH_OK);
    CHECK(wsh_source_bom(little) == WSH_BOM_UTF16_LE);
    CHECK(wsh_source_bom(big) == WSH_BOM_UTF16_BE);
    CHECK(view_equals_cstr(wsh_source_text(little), expected));
    CHECK(wsh_string_view_equal(
        wsh_source_text(little),
        wsh_source_text(big)));
    CHECK(wsh_source_scalar_count(little) == 4U);
    CHECK(wsh_source_line_count(little) == 2U);
    wsh_source_destroy(little);
    wsh_source_destroy(big);
    little = NULL;
    CHECK(wsh_source_create(
        NULL,
        NULL,
        unpaired,
        sizeof(unpaired),
        &little) == WSH_ERR_ENCODING);
    CHECK(wsh_source_create(
        NULL,
        NULL,
        odd,
        sizeof(odd),
        &little) == WSH_ERR_ENCODING);
    return 1;
}

/** Verify supplementary scalars through every M2 Unicode container. */
static int test_tc_0037(void)
{
    static const char supplementary[] =
        "\xF0\x90\x80\x80\xF0\x9F\x98\x80\xF4\x8F\xBF\xBD";
    static const char *const items[] = {supplementary};
    wsh_source *source = NULL;
    wsh_value *value = NULL;
    wsh_string_view element;
    uint16_t *units = NULL;
    size_t unit_count = 0U;
    char *round_trip = NULL;
    size_t byte_count = 0U;

    CHECK(wsh_source_create(
        NULL,
        NULL,
        (const unsigned char *)supplementary,
        sizeof(supplementary) - 1U,
        &source) == WSH_OK);
    CHECK(wsh_source_scalar_count(source) == 3U);
    CHECK(make_value(NULL, NULL, items, 1U, &value) == WSH_OK);
    CHECK(wsh_value_at(value, 0U, &element) == WSH_OK);
    CHECK(view_equals_cstr(element, supplementary));
    CHECK(wsh_utf8_to_utf16(
        NULL,
        NULL,
        element,
        &units,
        &unit_count) == WSH_OK);
    CHECK(unit_count == 6U);
    CHECK(wsh_utf16_to_utf8(
        NULL,
        NULL,
        units,
        unit_count,
        &round_trip,
        &byte_count) == WSH_OK);
    CHECK(byte_count == sizeof(supplementary) - 1U);
    CHECK(memcmp(round_trip, supplementary, byte_count) == 0);

    wsh_allocator_release(NULL, round_trip);
    wsh_allocator_release(NULL, units);
    wsh_value_destroy(value);
    wsh_source_destroy(source);
    return 1;
}

/** Verify private assignment, explicit export, and imported export state. */
static int test_tc_0046(void)
{
    static const char *const one_item[] = {"one"};
    static const char *const two_item[] = {"two"};
    wsh_context *context = NULL;
    wsh_value *one = NULL;
    wsh_value *two = NULL;
    int exported = -1;

    CHECK(make_value(NULL, NULL, one_item, 1U, &one) == WSH_OK);
    CHECK(make_value(NULL, NULL, two_item, 1U, &two) == WSH_OK);
    CHECK(wsh_context_create(NULL, &context) == WSH_OK);
    CHECK(wsh_context_set_variable(
        context,
        wsh_string_view_from_cstr("private"),
        one) == WSH_OK);
    CHECK(wsh_context_is_exported(
        context,
        wsh_string_view_from_cstr("private"),
        &exported) == WSH_OK);
    CHECK(exported == 0);
    CHECK(wsh_context_set_exported(
        context,
        wsh_string_view_from_cstr("private"),
        1) == WSH_OK);
    CHECK(wsh_context_set_variable(
        context,
        wsh_string_view_from_cstr("private"),
        two) == WSH_OK);
    CHECK(wsh_context_is_exported(
        context,
        wsh_string_view_from_cstr("private"),
        &exported) == WSH_OK);
    CHECK(exported == 1);
    CHECK(wsh_context_import_variable(
        context,
        wsh_string_view_from_cstr("imported"),
        one) == WSH_OK);
    CHECK(wsh_context_is_exported(
        context,
        wsh_string_view_from_cstr("imported"),
        &exported) == WSH_OK);
    CHECK(exported == 1);

    wsh_context_destroy(context);
    wsh_value_destroy(two);
    wsh_value_destroy(one);
    return 1;
}

/** Verify exact private identity and folded exported-name collision. */
static int test_tc_0048(void)
{
    static const char *const upper_item[] = {"upper"};
    static const char *const mixed_item[] = {"mixed"};
    wsh_fake_runtime *fake = NULL;
    wsh_context_options options;
    wsh_context *context = NULL;
    wsh_value *upper = NULL;
    wsh_value *mixed = NULL;
    const wsh_value *borrowed = NULL;
    wsh_string_view text;
    int exported = -1;

    CHECK(wsh_fake_runtime_create(NULL, NULL, &fake) == WSH_OK);
    wsh_context_options_init(&options);
    options.runtime = wsh_fake_runtime_interface(fake);
    CHECK(wsh_context_create(&options, &context) == WSH_OK);
    CHECK(make_value(NULL, NULL, upper_item, 1U, &upper) == WSH_OK);
    CHECK(make_value(NULL, NULL, mixed_item, 1U, &mixed) == WSH_OK);
    CHECK(wsh_context_set_variable(
        context,
        wsh_string_view_from_cstr("PATH"),
        upper) == WSH_OK);
    CHECK(wsh_context_set_variable(
        context,
        wsh_string_view_from_cstr("Path"),
        mixed) == WSH_OK);
    CHECK(wsh_context_get_variable(
        context,
        wsh_string_view_from_cstr("PATH"),
        &borrowed) == WSH_OK);
    CHECK(wsh_value_at(borrowed, 0U, &text) == WSH_OK);
    CHECK(view_equals_cstr(text, "upper"));
    CHECK(wsh_context_get_variable(
        context,
        wsh_string_view_from_cstr("Path"),
        &borrowed) == WSH_OK);
    CHECK(wsh_value_at(borrowed, 0U, &text) == WSH_OK);
    CHECK(view_equals_cstr(text, "mixed"));
    CHECK(wsh_context_set_exported(
        context,
        wsh_string_view_from_cstr("Path"),
        1) == WSH_OK);
    CHECK(wsh_context_set_exported(
        context,
        wsh_string_view_from_cstr("PATH"),
        1) == WSH_ERR_MISMATCH);
    CHECK(wsh_context_is_exported(
        context,
        wsh_string_view_from_cstr("PATH"),
        &exported) == WSH_OK);
    CHECK(exported == 0);
    CHECK(wsh_context_unset_variable(
        context,
        wsh_string_view_from_cstr("Path")) == WSH_OK);
    CHECK(wsh_context_set_exported(
        context,
        wsh_string_view_from_cstr("PATH"),
        1) == WSH_OK);

    wsh_value_destroy(mixed);
    wsh_value_destroy(upper);
    wsh_context_destroy(context);
    wsh_fake_runtime_destroy(fake);
    return 1;
}

/** Verify ordered unsigned statuses and the all-zero truth rule. */
static int test_tc_0049(void)
{
    static const uint32_t zeros[] = {0U, 0U};
    static const uint32_t mixed[] = {0U, 5U, UINT32_MAX};
    wsh_status_list *empty = NULL;
    wsh_status_list *success = NULL;
    wsh_status_list *failure = NULL;
    uint32_t value = 0U;

    CHECK(make_status(NULL, NULL, NULL, 0U, &empty) == WSH_OK);
    CHECK(make_status(NULL, NULL, zeros, 2U, &success) == WSH_OK);
    CHECK(make_status(NULL, NULL, mixed, 3U, &failure) == WSH_OK);
    CHECK(!wsh_status_list_is_success(empty));
    CHECK(wsh_status_list_last(empty, &value) == WSH_ERR_INVALID);
    CHECK(wsh_status_list_is_success(success));
    CHECK(!wsh_status_list_is_success(failure));
    CHECK(wsh_status_list_count(failure) == 3U);
    CHECK(wsh_status_list_at(failure, 1U, &value) == WSH_OK);
    CHECK(value == 5U);
    CHECK(wsh_status_list_last(failure, &value) == WSH_OK);
    CHECK(value == UINT32_MAX);

    wsh_status_list_destroy(failure);
    wsh_status_list_destroy(success);
    wsh_status_list_destroy(empty);
    return 1;
}

#if defined(_WIN32)
/** Create and mutate one context repeatedly on a dedicated worker thread. */
static DWORD WINAPI context_worker(LPVOID parameter)
{
    size_t seed = (size_t)(uintptr_t)parameter;
    static const char *const first_item[] = {"first"};
    static const char *const second_item[] = {"second"};
    const char *const *items = seed == 1U ? first_item : second_item;
    wsh_context *context = NULL;
    wsh_value *value = NULL;
    const wsh_value *borrowed = NULL;
    size_t index;

    if (make_value(NULL, NULL, items, 1U, &value) != WSH_OK ||
        wsh_context_create(NULL, &context) != WSH_OK) {
        wsh_value_destroy(value);
        wsh_context_destroy(context);
        return 1U;
    }
    for (index = 0U; index < 500U; ++index) {
        if (wsh_context_set_variable(
                context,
                wsh_string_view_from_cstr("worker"),
                value) != WSH_OK ||
            wsh_context_get_variable(
                context,
                wsh_string_view_from_cstr("worker"),
                &borrowed) != WSH_OK ||
            borrowed == NULL) {
            wsh_context_destroy(context);
            wsh_value_destroy(value);
            return 1U;
        }
    }
    wsh_context_destroy(context);
    wsh_value_destroy(value);
    return 0U;
}
#endif

/** Verify context isolation, deterministic runtime, and concurrency. */
static int test_tc_0070(void)
{
    static const char *const output_items[] = {"ok"};
    static const char *const left_items[] = {"left"};
    static const char *const right_items[] = {"right"};
    static const uint32_t status_items[] = {0U};
    wsh_fake_runtime *fake = NULL;
    wsh_context_options options;
    wsh_context *runtime_context = NULL;
    wsh_context *left_context = NULL;
    wsh_context *right_context = NULL;
    wsh_value *expected_output = NULL;
    wsh_status_list *expected_status = NULL;
    wsh_value *actual_output = NULL;
    wsh_status_list *actual_status = NULL;
    wsh_value *left_value = NULL;
    wsh_value *right_value = NULL;
    const wsh_value *borrowed = NULL;
    wsh_runtime_request request;
    wsh_string_view element;
    uint32_t status_value = 1U;
#if defined(_WIN32)
    HANDLE first_thread;
    HANDLE second_thread;
    DWORD first_result;
    DWORD second_result;
#endif

    CHECK(wsh_fake_runtime_create(NULL, NULL, &fake) == WSH_OK);
    CHECK(make_value(
        NULL,
        NULL,
        output_items,
        1U,
        &expected_output) == WSH_OK);
    CHECK(make_status(
        NULL,
        NULL,
        status_items,
        1U,
        &expected_status) == WSH_OK);
    CHECK(wsh_fake_runtime_expect(
        fake,
        WSH_RUNTIME_LAUNCH,
        wsh_string_view_from_cstr("echo"),
        expected_output,
        expected_status,
        WSH_OK) == WSH_OK);
    wsh_context_options_init(&options);
    options.runtime = wsh_fake_runtime_interface(fake);
    CHECK(wsh_context_create(&options, &runtime_context) == WSH_OK);
    memset(&request, 0, sizeof(request));
    request.operation = WSH_RUNTIME_LAUNCH;
    request.subject = wsh_string_view_from_cstr("echo");
    CHECK(wsh_context_runtime_invoke(
        runtime_context,
        &request,
        &actual_output,
        &actual_status) == WSH_OK);
    CHECK(wsh_value_at(actual_output, 0U, &element) == WSH_OK);
    CHECK(view_equals_cstr(element, "ok"));
    CHECK(wsh_status_list_last(actual_status, &status_value) == WSH_OK);
    CHECK(status_value == 0U);
    CHECK(wsh_fake_runtime_complete(fake) == WSH_OK);
    CHECK(wsh_fake_runtime_call_count(fake) == 1U);
    wsh_value_destroy(actual_output);
    wsh_status_list_destroy(actual_status);
    actual_output = NULL;
    actual_status = NULL;
    CHECK(wsh_context_runtime_invoke(
        runtime_context,
        &request,
        &actual_output,
        &actual_status) == WSH_ERR_MISMATCH);
    CHECK(actual_output == NULL && actual_status == NULL);

    CHECK(make_value(
        NULL,
        NULL,
        left_items,
        1U,
        &left_value) == WSH_OK);
    CHECK(make_value(
        NULL,
        NULL,
        right_items,
        1U,
        &right_value) == WSH_OK);
    CHECK(wsh_context_create(NULL, &left_context) == WSH_OK);
    CHECK(wsh_context_create(NULL, &right_context) == WSH_OK);
    CHECK(wsh_context_set_variable(
        left_context,
        wsh_string_view_from_cstr("same"),
        left_value) == WSH_OK);
    CHECK(wsh_context_set_variable(
        right_context,
        wsh_string_view_from_cstr("same"),
        right_value) == WSH_OK);
    CHECK(wsh_context_get_variable(
        left_context,
        wsh_string_view_from_cstr("same"),
        &borrowed) == WSH_OK);
    CHECK(wsh_value_at(borrowed, 0U, &element) == WSH_OK);
    CHECK(view_equals_cstr(element, "left"));
    CHECK(wsh_context_get_variable(
        right_context,
        wsh_string_view_from_cstr("same"),
        &borrowed) == WSH_OK);
    CHECK(wsh_value_at(borrowed, 0U, &element) == WSH_OK);
    CHECK(view_equals_cstr(element, "right"));

#if defined(_WIN32)
    first_thread = CreateThread(
        NULL,
        0U,
        context_worker,
        (LPVOID)(uintptr_t)1U,
        0U,
        NULL);
    second_thread = CreateThread(
        NULL,
        0U,
        context_worker,
        (LPVOID)(uintptr_t)2U,
        0U,
        NULL);
    CHECK(first_thread != NULL && second_thread != NULL);
    CHECK(WaitForSingleObject(first_thread, INFINITE) == WAIT_OBJECT_0);
    CHECK(WaitForSingleObject(second_thread, INFINITE) == WAIT_OBJECT_0);
    CHECK(GetExitCodeThread(first_thread, &first_result) != 0);
    CHECK(GetExitCodeThread(second_thread, &second_result) != 0);
    CHECK(first_result == 0U && second_result == 0U);
    CloseHandle(first_thread);
    CloseHandle(second_thread);
#else
    CHECK(0 && "TC-0070 requires a native Windows thread host");
#endif

    wsh_context_destroy(right_context);
    wsh_context_destroy(left_context);
    wsh_value_destroy(right_value);
    wsh_value_destroy(left_value);
    wsh_context_destroy(runtime_context);
    wsh_status_list_destroy(expected_status);
    wsh_value_destroy(expected_output);
    wsh_fake_runtime_destroy(fake);
    return 1;
}

/** Verify every M2 collection at and beyond its configured bound. */
static int test_tc_0074(void)
{
    static const unsigned char four_bytes[] = "1234";
    static const unsigned char five_bytes[] = "12345";
    static const char *const one_item[] = {"x"};
    static const uint32_t status_items[] = {0U};
    wsh_limits limits = wsh_limits_default();
    wsh_source *source = NULL;
    wsh_string *string = NULL;
    wsh_value_builder *value_builder = NULL;
    wsh_value *value = NULL;
    wsh_status_list *status = NULL;
    wsh_context_options options;
    wsh_context *context = NULL;
    wsh_fake_runtime *fake = NULL;
    wsh_runtime_request request;
    wsh_value *runtime_value = NULL;
    wsh_status_list *runtime_status = NULL;

    limits.max_source_bytes = 4U;
    CHECK(wsh_source_create(
        NULL,
        &limits,
        four_bytes,
        4U,
        &source) == WSH_OK);
    wsh_source_destroy(source);
    source = NULL;
    CHECK(wsh_source_create(
        NULL,
        &limits,
        five_bytes,
        5U,
        &source) == WSH_ERR_RESOURCE);
    limits = wsh_limits_default();
    limits.max_string_bytes = 3U;
    CHECK(wsh_string_create(
        NULL,
        &limits,
        wsh_string_view_from_cstr("abc"),
        &string) == WSH_OK);
    wsh_string_destroy(string);
    string = NULL;
    CHECK(wsh_string_create(
        NULL,
        &limits,
        wsh_string_view_from_cstr("abcd"),
        &string) == WSH_ERR_RESOURCE);

    limits = wsh_limits_default();
    limits.max_list_items = 2U;
    CHECK(wsh_value_builder_create(NULL, &limits, &value_builder) == WSH_OK);
    CHECK(wsh_value_builder_append(
        value_builder,
        wsh_string_view_from_cstr("a")) == WSH_OK);
    CHECK(wsh_value_builder_append(
        value_builder,
        wsh_string_view_from_cstr("b")) == WSH_OK);
    CHECK(wsh_value_builder_append(
        value_builder,
        wsh_string_view_from_cstr("c")) == WSH_ERR_RESOURCE);
    CHECK(wsh_value_builder_finish(value_builder, &value) == WSH_OK);
    CHECK(wsh_value_count(value) == 2U);
    wsh_value_builder_destroy(value_builder);
    value_builder = NULL;

    limits.max_variables = 2U;
    limits.max_diagnostics = 1U;
    limits.max_runtime_expectations = 1U;
    limits.max_runtime_calls = 1U;
    CHECK(wsh_fake_runtime_create(NULL, &limits, &fake) == WSH_OK);
    CHECK(make_status(NULL, &limits, status_items, 1U, &status) == WSH_OK);
    CHECK(wsh_fake_runtime_expect(
        fake,
        WSH_RUNTIME_CLOCK,
        wsh_string_view_from_cstr("now"),
        value,
        status,
        WSH_OK) == WSH_OK);
    CHECK(wsh_fake_runtime_expect(
        fake,
        WSH_RUNTIME_CLOCK,
        wsh_string_view_from_cstr("later"),
        value,
        status,
        WSH_OK) == WSH_ERR_RESOURCE);
    wsh_context_options_init(&options);
    options.limits = limits;
    options.runtime = wsh_fake_runtime_interface(fake);
    CHECK(wsh_context_create(&options, &context) == WSH_OK);
    CHECK(wsh_context_set_variable(
        context,
        wsh_string_view_from_cstr("one"),
        value) == WSH_OK);
    CHECK(wsh_context_set_variable(
        context,
        wsh_string_view_from_cstr("two"),
        value) == WSH_OK);
    CHECK(wsh_context_set_variable(
        context,
        wsh_string_view_from_cstr("three"),
        value) == WSH_ERR_RESOURCE);
    CHECK(wsh_context_variable_count(context) == 2U);
    CHECK(wsh_context_add_diagnostic(
        context,
        WSH_DIAGNOSTIC_NOTE,
        WSH_DIAGNOSTIC_LIMIT,
        wsh_string_view_from_cstr("one"),
        wsh_string_view_from_cstr(""),
        NULL) == WSH_OK);
    CHECK(wsh_context_add_diagnostic(
        context,
        WSH_DIAGNOSTIC_NOTE,
        WSH_DIAGNOSTIC_LIMIT,
        wsh_string_view_from_cstr("two"),
        wsh_string_view_from_cstr(""),
        NULL) == WSH_ERR_RESOURCE);
    memset(&request, 0, sizeof(request));
    request.operation = WSH_RUNTIME_CLOCK;
    request.subject = wsh_string_view_from_cstr("now");
    CHECK(wsh_context_runtime_invoke(
        context,
        &request,
        &runtime_value,
        &runtime_status) == WSH_OK);
    wsh_value_destroy(runtime_value);
    wsh_status_list_destroy(runtime_status);
    runtime_value = NULL;
    runtime_status = NULL;
    CHECK(wsh_context_runtime_invoke(
        context,
        &request,
        &runtime_value,
        &runtime_status) == WSH_ERR_RESOURCE);

    wsh_context_destroy(context);
    wsh_fake_runtime_destroy(fake);
    wsh_status_list_destroy(status);
    wsh_value_destroy(value);
    return 1;
}

/** Copy a nullable locale query into a bounded local observation. */
static void capture_locale(char *buffer, size_t capacity)
{
    const char *value = setlocale(LC_ALL, NULL);
    size_t length;

    if (capacity == 0U) {
        return;
    }
    if (value == NULL) {
        buffer[0] = '\0';
        return;
    }
    length = strlen(value);
    if (length >= capacity) {
        length = capacity - 1U;
    }
    memcpy(buffer, value, length);
    buffer[length] = '\0';
}

/** Verify hostile input failure and absence of process-global mutation. */
static int test_tc_0075(void)
{
    static const unsigned char invalid_utf8[][4] = {
        {0x80U, 0U, 0U, 0U},
        {0xC0U, 0x80U, 0U, 0U},
        {0xE0U, 0x80U, 0x80U, 0U},
        {0xEDU, 0xA0U, 0x80U, 0U},
        {0xF4U, 0x90U, 0x80U, 0x80U}
    };
    static const size_t invalid_lengths[] = {1U, 2U, 3U, 3U, 4U};
    static const char *const output_items[] = {"safe"};
    static const uint32_t status_items[] = {0U};
    wsh_source *source = NULL;
    size_t index;
    wsh_fake_runtime *fake = NULL;
    wsh_value *output = NULL;
    wsh_status_list *status = NULL;
    wsh_context_options options;
    wsh_context *context = NULL;
    wsh_runtime_request request;
    wsh_value *actual_output = NULL;
    wsh_status_list *actual_status = NULL;
    char locale_before[128];
    char locale_after[128];
#if defined(_WIN32)
    wchar_t directory_before[MAX_PATH];
    wchar_t directory_after[MAX_PATH];
    wchar_t environment_before[64];
    wchar_t environment_after[64];
    DWORD directory_before_length;
    DWORD directory_after_length;
    DWORD environment_before_length;
    DWORD environment_after_length;
    UINT input_code_page;
    UINT output_code_page;
    HANDLE standard_input;
    HANDLE standard_output;
    HANDLE standard_error;
#endif

    capture_locale(locale_before, sizeof(locale_before));
#if defined(_WIN32)
    directory_before_length = GetCurrentDirectoryW(
        MAX_PATH,
        directory_before);
    environment_before_length = GetEnvironmentVariableW(
        L"WSH_M2_GLOBAL_STATE_SENTINEL",
        environment_before,
        64U);
    input_code_page = GetConsoleCP();
    output_code_page = GetConsoleOutputCP();
    standard_input = GetStdHandle(STD_INPUT_HANDLE);
    standard_output = GetStdHandle(STD_OUTPUT_HANDLE);
    standard_error = GetStdHandle(STD_ERROR_HANDLE);
#endif

    for (index = 0U; index <
            sizeof(invalid_lengths) / sizeof(invalid_lengths[0]);
            ++index) {
        CHECK(wsh_source_create(
            NULL,
            NULL,
            invalid_utf8[index],
            invalid_lengths[index],
            &source) == WSH_ERR_ENCODING);
        CHECK(source == NULL);
    }
    CHECK(wsh_source_create(
        NULL,
        NULL,
        (const unsigned char *)"x",
        (size_t)-1,
        &source) == WSH_ERR_RESOURCE);
    CHECK(source == NULL);

    CHECK(wsh_fake_runtime_create(NULL, NULL, &fake) == WSH_OK);
    CHECK(make_value(NULL, NULL, output_items, 1U, &output) == WSH_OK);
    CHECK(make_status(NULL, NULL, status_items, 1U, &status) == WSH_OK);
    CHECK(wsh_fake_runtime_expect(
        fake,
        WSH_RUNTIME_LAUNCH,
        wsh_string_view_from_cstr("not-a-real-process"),
        output,
        status,
        WSH_OK) == WSH_OK);
    wsh_context_options_init(&options);
    options.runtime = wsh_fake_runtime_interface(fake);
    CHECK(wsh_context_create(&options, &context) == WSH_OK);
    memset(&request, 0, sizeof(request));
    request.operation = WSH_RUNTIME_LAUNCH;
    request.subject = wsh_string_view_from_cstr("not-a-real-process");
    CHECK(wsh_context_runtime_invoke(
        context,
        &request,
        &actual_output,
        &actual_status) == WSH_OK);
    CHECK(wsh_fake_runtime_complete(fake) == WSH_OK);

    capture_locale(locale_after, sizeof(locale_after));
    CHECK(strcmp(locale_before, locale_after) == 0);
#if defined(_WIN32)
    directory_after_length = GetCurrentDirectoryW(MAX_PATH, directory_after);
    environment_after_length = GetEnvironmentVariableW(
        L"WSH_M2_GLOBAL_STATE_SENTINEL",
        environment_after,
        64U);
    CHECK(directory_before_length == directory_after_length);
    CHECK(wcscmp(directory_before, directory_after) == 0);
    CHECK(environment_before_length == environment_after_length);
    if (environment_before_length != 0U &&
        environment_before_length < 64U) {
        CHECK(wcscmp(environment_before, environment_after) == 0);
    }
    CHECK(input_code_page == GetConsoleCP());
    CHECK(output_code_page == GetConsoleOutputCP());
    CHECK(standard_input == GetStdHandle(STD_INPUT_HANDLE));
    CHECK(standard_output == GetStdHandle(STD_OUTPUT_HANDLE));
    CHECK(standard_error == GetStdHandle(STD_ERROR_HANDLE));
#endif

    wsh_status_list_destroy(actual_status);
    wsh_value_destroy(actual_output);
    wsh_context_destroy(context);
    wsh_status_list_destroy(status);
    wsh_value_destroy(output);
    wsh_fake_runtime_destroy(fake);
    return 1;
}

/** Controlled M2 test inventory. */
static const test_case_entry test_cases[] = {
    {"TC-0007", test_tc_0007},
    {"TC-0018", test_tc_0018},
    {"TC-0019", test_tc_0019},
    {"TC-0024", test_tc_0024},
    {"TC-0034", test_tc_0034},
    {"TC-0035", test_tc_0035},
    {"TC-0036", test_tc_0036},
    {"TC-0037", test_tc_0037},
    {"TC-0046", test_tc_0046},
    {"TC-0048", test_tc_0048},
    {"TC-0049", test_tc_0049},
    {"TC-0070", test_tc_0070},
    {"TC-0074", test_tc_0074},
    {"TC-0075", test_tc_0075}
};

/**
 * Execute one named controlled case or the entire inventory.
 * @param argc Argument count.
 * @param argv Optional test-case identifier in argv[1].
 * @return Zero only when every selected case passes.
 */
int main(int argc, char **argv)
{
    size_t index;
    int selected = 0;

    for (index = 0U;
            index < sizeof(test_cases) / sizeof(test_cases[0]);
            ++index) {
        if (argc > 1 && strcmp(argv[1], test_cases[index].identifier) != 0) {
            continue;
        }
        selected = 1;
        if (!test_cases[index].function()) {
            fprintf(stderr, "[FAIL] %s\n", test_cases[index].identifier);
            return 1;
        }
        printf("[PASS] %s\n", test_cases[index].identifier);
    }
    if (!selected) {
        fprintf(stderr, "Unknown test case\n");
        return 2;
    }
    return 0;
}
