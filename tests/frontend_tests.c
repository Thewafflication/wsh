/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file frontend_tests.c
 * @brief Deterministic tests for interactive and batch input sessions.
 */

#include "frontend.h"

#include <stdio.h>
#include <string.h>

#define CHECK(condition) \
    do { \
        if (!(condition)) { \
            fprintf(stderr, "check failed at line %d: %s\n", \
                __LINE__, #condition); \
            return 0; \
        } \
    } while (0)

/** Scripted line source used by one test. */
typedef struct scripted_input {
    /** Borrowed line strings. */
    const char **lines;
    /** Number of lines. */
    size_t count;
    /** Next line index. */
    size_t index;
    /** Result returned after the last line. */
    wsh_frontend_read_result final_result;
} scripted_input;

/** Fixed output capture used by one test. */
typedef struct captured_output {
    /** Retained text bytes. */
    char bytes[4096];
    /** Retained byte count. */
    size_t length;
    /** Nonzero makes every write fail. */
    int fail;
} captured_output;

/** Evaluation observations used by one test. */
typedef struct evaluation_state {
    /** Number of complete trees received. */
    size_t count;
    /** Status returned for every tree. */
    int status;
} evaluation_state;

/** Return the next scripted line or final source result. */
static wsh_frontend_read_result read_scripted_line(
    void *user_data,
    const unsigned char **out_bytes,
    size_t *out_length)
{
    scripted_input *input;

    input = (scripted_input *)user_data;
    *out_bytes = NULL;
    *out_length = 0U;
    if (input->index == input->count) {
        return input->final_result;
    }
    *out_bytes = (const unsigned char *)input->lines[input->index];
    *out_length = strlen(input->lines[input->index]);
    input->index += 1U;
    return WSH_FRONTEND_READ_LINE;
}

/** Append output to a bounded deterministic capture. */
static int capture_write(void *user_data, const char *bytes, size_t length)
{
    captured_output *output;

    output = (captured_output *)user_data;
    if (output->fail || length > sizeof(output->bytes) - output->length - 1U) {
        return 1;
    }
    if (length > 0U) {
        memcpy(output->bytes + output->length, bytes, length);
    }
    output->length += length;
    output->bytes[output->length] = '\0';
    return 0;
}

/** Record one complete AST delivered across the front-end boundary. */
static int record_evaluation(
    void *user_data,
    const wsh_parse_tree *tree)
{
    evaluation_state *state;

    state = (evaluation_state *)user_data;
    if (wsh_parse_tree_status(tree) != WSH_SYNTAX_COMPLETE ||
        wsh_parse_tree_root(tree) == NULL) {
        return 1;
    }
    state->count += 1U;
    return state->status;
}

/** Fill session dependencies with deterministic test adapters. */
static void prepare_io(
    wsh_frontend_io *io,
    scripted_input *input,
    captured_output *output,
    captured_output *error,
    evaluation_state *evaluation)
{
    memset(io, 0, sizeof(*io));
    io->input_data = input;
    io->read_line = read_scripted_line;
    io->output_data = output;
    io->write_output = capture_write;
    io->error_data = error;
    io->write_error = capture_write;
    io->evaluation_data = evaluation;
    io->evaluate = record_evaluation;
}

/** Verify defaults and invalid session contracts. */
static int test_options_and_contract(void)
{
    wsh_frontend_options options;

    memset(&options, 0, sizeof(options));
    wsh_frontend_options_init(&options);
    CHECK(options.interactive == 0);
    CHECK(strcmp(options.primary_prompt, "% ") == 0);
    CHECK(strcmp(options.continuation_prompt, "; ") == 0);
    CHECK(options.max_command_bytes == 16U * 1024U * 1024U);
    CHECK(wsh_frontend_run(NULL, NULL) == 1);
    return 1;
}

/** Verify batch input delivers each complete tree without prompts. */
static int test_batch_complete(void)
{
    const char *lines[] = {"echo one\n", "echo two\n"};
    scripted_input input = {lines, 2U, 0U, WSH_FRONTEND_READ_EOF};
    captured_output output = {{0}, 0U, 0};
    captured_output error = {{0}, 0U, 0};
    evaluation_state evaluation = {0U, 0};
    wsh_frontend_options options;
    wsh_frontend_io io;

    prepare_io(&io, &input, &output, &error, &evaluation);
    wsh_frontend_options_init(&options);
    CHECK(wsh_frontend_run(&options, &io) == 0);
    CHECK(evaluation.count == 2U);
    CHECK(output.length == 0U);
    CHECK(error.length == 0U);
    return 1;
}

/** Verify incomplete input selects continuation prompts then recovers. */
static int test_interactive_multiline(void)
{
    const char *lines[] = {"echo 'first\n", "second'\n"};
    scripted_input input = {lines, 2U, 0U, WSH_FRONTEND_READ_EOF};
    captured_output output = {{0}, 0U, 0};
    captured_output error = {{0}, 0U, 0};
    evaluation_state evaluation = {0U, 0};
    wsh_frontend_options options;
    wsh_frontend_io io;

    prepare_io(&io, &input, &output, &error, &evaluation);
    wsh_frontend_options_init(&options);
    options.interactive = 1;
    CHECK(wsh_frontend_run(&options, &io) == 0);
    CHECK(evaluation.count == 1U);
    CHECK(strcmp(output.bytes, "% ; % ") == 0);
    CHECK(error.length == 0U);
    return 1;
}

/** Verify interactive syntax errors are discarded and do not end input. */
static int test_interactive_recovery(void)
{
    const char *lines[] = {"}\n", "echo recovered\n"};
    scripted_input input = {lines, 2U, 0U, WSH_FRONTEND_READ_EOF};
    captured_output output = {{0}, 0U, 0};
    captured_output error = {{0}, 0U, 0};
    evaluation_state evaluation = {0U, 0};
    wsh_frontend_options options;
    wsh_frontend_io io;

    prepare_io(&io, &input, &output, &error, &evaluation);
    wsh_frontend_options_init(&options);
    options.interactive = 1;
    CHECK(wsh_frontend_run(&options, &io) == 0);
    CHECK(evaluation.count == 1U);
    CHECK(strcmp(output.bytes, "% % % ") == 0);
    CHECK(strstr(error.bytes, "wsh: 1:1:") != NULL);
    return 1;
}

/** Verify incomplete batch EOF is a syntax failure with a diagnostic. */
static int test_incomplete_eof(void)
{
    const char *lines[] = {"echo 'unterminated"};
    scripted_input input = {lines, 1U, 0U, WSH_FRONTEND_READ_EOF};
    captured_output output = {{0}, 0U, 0};
    captured_output error = {{0}, 0U, 0};
    evaluation_state evaluation = {0U, 0};
    wsh_frontend_options options;
    wsh_frontend_io io;

    prepare_io(&io, &input, &output, &error, &evaluation);
    wsh_frontend_options_init(&options);
    CHECK(wsh_frontend_run(&options, &io) == 3);
    CHECK(evaluation.count == 0U);
    CHECK(strstr(error.bytes, "unterminated") != NULL);
    return 1;
}

/** Verify stable mappings for resource, encoding, read, and write failures. */
static int test_failure_mapping(void)
{
    const char *large_lines[] = {"echo"};
    scripted_input input = {
        large_lines, 1U, 0U, WSH_FRONTEND_READ_EOF};
    captured_output output = {{0}, 0U, 0};
    captured_output error = {{0}, 0U, 0};
    evaluation_state evaluation = {0U, 0};
    wsh_frontend_options options;
    wsh_frontend_io io;

    prepare_io(&io, &input, &output, &error, &evaluation);
    wsh_frontend_options_init(&options);
    options.max_command_bytes = 3U;
    CHECK(wsh_frontend_run(&options, &io) == 9);

    input.count = 0U;
    input.index = 0U;
    input.final_result = WSH_FRONTEND_READ_ENCODING;
    options.max_command_bytes = 16U;
    CHECK(wsh_frontend_run(&options, &io) == 6);

    input.final_result = WSH_FRONTEND_READ_ERROR;
    CHECK(wsh_frontend_run(&options, &io) == 5);

    input.final_result = WSH_FRONTEND_READ_EOF;
    options.interactive = 1;
    output.fail = 1;
    CHECK(wsh_frontend_run(&options, &io) == 5);
    return 1;
}

/** Run the deterministic front-end suite. */
int main(void)
{
    if (!test_options_and_contract() ||
        !test_batch_complete() ||
        !test_interactive_multiline() ||
        !test_interactive_recovery() ||
        !test_incomplete_eof() ||
        !test_failure_mapping()) {
        return 1;
    }
    printf("frontend tests passed\n");
    return 0;
}
