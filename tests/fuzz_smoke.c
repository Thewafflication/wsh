/**
 * @file fuzz_smoke.c
 * @brief Deterministic input fuzzing smoke harness for the portable front end.
 *
 * Drives untrusted byte sequences through the public decode, lex, and parse
 * surface and walks any resulting AST. It asserts that every input is either
 * rejected cleanly or produces a bounded, freed result, and that adversarial
 * inputs — deeply nested constructs, long runs, and unbalanced quoting — reach
 * a defined status instead of unbounded recursion, a crash, or a leak. The seed
 * is fixed so a failure reproduces exactly.
 */

#include "wsh/core.h"
#include "wsh/parser.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/** Deterministic xorshift64 state. */
static uint64_t g_rng = 0x9E3779B97F4A7C15ULL;

/** Return the next pseudo-random 32-bit value. */
static uint32_t next_rand(void)
{
    g_rng ^= g_rng << 13;
    g_rng ^= g_rng >> 7;
    g_rng ^= g_rng << 17;
    return (uint32_t)(g_rng >> 11);
}

/** Alphabet biased toward WSH syntax so the lexer and parser are exercised. */
static const char g_alphabet[] =
    "abc  \t\n{}();&|!@^`<>'=$#ABC012\"\\/:.";

/** Recursively visit every AST node under a bounded depth. */
static void walk_node(const wsh_ast_node *node, unsigned depth)
{
    size_t count;
    size_t index;

    if (node == NULL || depth > WSH_PARSER_MAX_PARSE_DEPTH) {
        return;
    }
    (void)wsh_ast_node_kind(node);
    (void)wsh_ast_node_text(node);
    count = wsh_ast_node_child_count(node);
    for (index = 0U; index < count; ++index) {
        walk_node(wsh_ast_node_child_at(node, index), depth + 1U);
    }
}

/** Decode, lex, parse, walk, and format one input; free everything. */
static void drive(const unsigned char *bytes, size_t length)
{
    wsh_source *source = NULL;
    wsh_token_stream *stream = NULL;
    wsh_parse_tree *tree = NULL;

    if (wsh_source_create(NULL, NULL, bytes, length, &source) != WSH_OK) {
        return;  /* Invalid input rejected cleanly is an acceptable outcome. */
    }

    if (wsh_lex(NULL, source, &stream) == WSH_OK) {
        size_t count = wsh_token_stream_count(stream);
        size_t index;
        for (index = 0U; index < count; ++index) {
            wsh_token_view token;
            (void)wsh_token_stream_at(stream, index, &token);
        }
        (void)wsh_token_stream_status(stream);
        wsh_token_stream_destroy(stream);
    }

    if (wsh_parse(NULL, source, &tree) == WSH_OK) {
        if (wsh_parse_tree_status(tree) == WSH_SYNTAX_COMPLETE) {
            wsh_string *formatted = NULL;
            walk_node(wsh_parse_tree_root(tree), 0U);
            if (wsh_parse_tree_format(tree, &formatted) == WSH_OK) {
                wsh_string_destroy(formatted);
            }
        }
        wsh_parse_tree_destroy(tree);
    }

    wsh_source_destroy(source);
}

/** Drive a NUL-terminated literal input. */
static void drive_literal(const char *text)
{
    drive((const unsigned char *)text, strlen(text));
}

/** Drive a long run of one repeated character without unbounded recursion. */
static int drive_repeated(char character, size_t length)
{
    unsigned char *buffer = (unsigned char *)malloc(length);
    wsh_source *source = NULL;
    wsh_parse_tree *tree = NULL;
    int survived = 1;

    if (buffer == NULL) {
        return 0;
    }
    memset(buffer, (int)(unsigned char)character, length);

    /* A pathological nesting depth must reach a defined status, not overflow. */
    if (wsh_source_create(NULL, NULL, buffer, length, &source) == WSH_OK) {
        if (wsh_parse(NULL, source, &tree) == WSH_OK) {
            wsh_syntax_status status = wsh_parse_tree_status(tree);
            if (status != WSH_SYNTAX_COMPLETE &&
                status != WSH_SYNTAX_INCOMPLETE &&
                status != WSH_SYNTAX_ERROR) {
                survived = 0;
            }
            wsh_parse_tree_destroy(tree);
        }
        wsh_source_destroy(source);
    }
    free(buffer);
    return survived;
}

int main(void)
{
    static const char *const corpus[] = {
        "", " ", "\n", "'", "''", "'''", "echo hi", "a|b|c", "a && b || c",
        "{ { { } } }", "( ( ( ) ) )", "for i in `seq` { echo $i }",
        "if { a } { b }", "x = (1 2 3)", "cmd <in >out >>app",
        "@ subshell &", "`inner`", "$var $#var $^var", "!!!!!!!!",
        "a^b^c", "cmd 2>&1", "'unterminated", "=", ";;;;", "&|<>^@!"
    };
    const size_t corpus_count = sizeof(corpus) / sizeof(corpus[0]);
    size_t index;
    unsigned iteration;
    unsigned long total = 0UL;

    for (index = 0U; index < corpus_count; ++index) {
        drive_literal(corpus[index]);
        ++total;
    }

    /* Pathological depth and length must be bounded, not fatal. */
    if (!drive_repeated('{', 100000U) ||
        !drive_repeated('(', 100000U) ||
        !drive_repeated('\'', 100000U) ||
        !drive_repeated('a', 200000U)) {
        fprintf(stderr, "fuzz-smoke: pathological input was not bounded\n");
        return 1;
    }
    total += 4UL;

    for (iteration = 0U; iteration < 30000U; ++iteration) {
        unsigned char input[256];
        size_t length = (size_t)(next_rand() % sizeof(input));
        size_t position;
        for (position = 0U; position < length; ++position) {
            input[position] =
                (unsigned char)g_alphabet[next_rand() % (sizeof(g_alphabet) - 1U)];
        }
        drive(input, length);
        ++total;
    }

    printf("fuzz-smoke: %lu inputs decoded, lexed, and parsed without fault\n",
        total);
    return 0;
}
