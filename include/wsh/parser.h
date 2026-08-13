/**
 * @file parser.h
 * @brief Portable WSH lexer, parser, and immutable AST contracts.
 *
 * The interfaces in this file accept only a decoded wsh_source, allocator,
 * and numeric limits. They perform no runtime or operating-system operation.
 */

#ifndef WSH_PARSER_H
#define WSH_PARSER_H

#include <stddef.h>

#include "wsh/core.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Highest portable recursive parse-depth setting. */
#define WSH_PARSER_MAX_PARSE_DEPTH 512U

/** Final syntactic classification of a token stream or parse tree. */
typedef enum wsh_syntax_status {
    /** The complete source is lexically and syntactically valid. */
    WSH_SYNTAX_COMPLETE = 0,
    /** More source can complete the currently open construct. */
    WSH_SYNTAX_INCOMPLETE = 1,
    /** The source contains a non-continuable lexical or syntax error. */
    WSH_SYNTAX_ERROR = 2
} wsh_syntax_status;

/** Stable syntax diagnostic identifiers. */
typedef enum wsh_syntax_diagnostic_code {
    /** A lexical form is malformed. */
    WSH_SYNTAX_DIAGNOSTIC_LEXICAL = 2001,
    /** A grammar production received an unexpected token. */
    WSH_SYNTAX_DIAGNOSTIC_UNEXPECTED = 2002,
    /** End of source occurred while a construct remained open. */
    WSH_SYNTAX_DIAGNOSTIC_INCOMPLETE = 2003,
    /** A parser-specific resource ceiling was reached. */
    WSH_SYNTAX_DIAGNOSTIC_LIMIT = 2004
} wsh_syntax_diagnostic_code;

/** Parser allocation and resource policy. */
typedef struct wsh_parser_options {
    /** Allocator copied into every resulting owner. */
    wsh_allocator allocator;
    /** Maximum tokens, including the EOF token. */
    size_t max_tokens;
    /** Maximum nodes in one parse tree. */
    size_t max_ast_nodes;
    /** Maximum recursive grammar depth, at most the portable maximum. */
    size_t max_parse_depth;
    /** Maximum retained diagnostics. M3 currently emits at most one. */
    size_t max_diagnostics;
} wsh_parser_options;

/** Lexical token categories. */
typedef enum wsh_token_kind {
    /** End of normalized source. */
    WSH_TOKEN_EOF = 0,
    /** Normalized source line ending. */
    WSH_TOKEN_NEWLINE,
    /** Unquoted ordinary word. */
    WSH_TOKEN_WORD,
    /** Apostrophe-quoted and decoded word. */
    WSH_TOKEN_QUOTED_WORD,
    /** Variable expansion with the prefix removed from text. */
    WSH_TOKEN_VARIABLE,
    /** Variable element-count expansion. */
    WSH_TOKEN_COUNT,
    /** Variable flattening expansion. */
    WSH_TOKEN_FLATTEN,
    /** Semicolon list separator. */
    WSH_TOKEN_SEMICOLON,
    /** Background list separator. */
    WSH_TOKEN_AMPERSAND,
    /** Conditional-and operator. */
    WSH_TOKEN_AND_IF,
    /** Conditional-or operator. */
    WSH_TOKEN_OR_IF,
    /** Pipeline operator. */
    WSH_TOKEN_PIPE,
    /** Logical inversion prefix. */
    WSH_TOKEN_BANG,
    /** Semantic subshell prefix. */
    WSH_TOKEN_AT,
    /** Explicit caret concatenation. */
    WSH_TOKEN_CARET,
    /** Command substitution prefix. */
    WSH_TOKEN_BACKQUOTE,
    /** Left brace. */
    WSH_TOKEN_LEFT_BRACE,
    /** Right brace. */
    WSH_TOKEN_RIGHT_BRACE,
    /** Left parenthesis. */
    WSH_TOKEN_LEFT_PAREN,
    /** Right parenthesis. */
    WSH_TOKEN_RIGHT_PAREN,
    /** Input redirection operator. */
    WSH_TOKEN_LESS,
    /** Output redirection operator. */
    WSH_TOKEN_GREATER,
    /** Append redirection operator. */
    WSH_TOKEN_APPEND,
    /** Here-document redirection operator. */
    WSH_TOKEN_HERE,
    /** Duplex process-substitution prefix. */
    WSH_TOKEN_DUPLEX,
    /** Read process-substitution prefix. */
    WSH_TOKEN_PROCESS_READ,
    /** Write process-substitution prefix. */
    WSH_TOKEN_PROCESS_WRITE,
    /** Duplex process-substitution prefix. */
    WSH_TOKEN_PROCESS_DUPLEX
} wsh_token_kind;

/** Borrowed immutable token information. */
typedef struct wsh_token_view {
    /** Token category. */
    wsh_token_kind kind;
    /** Decoded or exact token text, excluding its syntactic prefix. */
    wsh_string_view text;
    /** Here-document body associated with a marker token, otherwise empty. */
    wsh_string_view auxiliary;
    /** Half-open source range occupied by the token spelling. */
    wsh_source_span span;
} wsh_token_view;

/** Borrowed immutable lexer/parser diagnostic. */
typedef struct wsh_syntax_diagnostic_view {
    /** Stable diagnostic identifier. */
    wsh_syntax_diagnostic_code code;
    /** Validated UTF-8 diagnostic message. */
    wsh_string_view message;
    /** Source range that caused or exposed the condition. */
    wsh_source_span span;
} wsh_syntax_diagnostic_view;

/** Opaque immutable token-stream owner. */
typedef struct wsh_token_stream wsh_token_stream;

/** Immutable AST node categories. */
typedef enum wsh_ast_kind {
    /** Complete source root. */
    WSH_AST_INPUT = 0,
    /** Ordered command list. */
    WSH_AST_COMMAND_LIST,
    /** Background command wrapper. */
    WSH_AST_BACKGROUND,
    /** Conditional-and expression. */
    WSH_AST_AND,
    /** Conditional-or expression. */
    WSH_AST_OR,
    /** Ordered pipeline. */
    WSH_AST_PIPELINE,
    /** Decoration between two pipeline stages. */
    WSH_AST_PIPE_OPERATOR,
    /** Logical inversion. */
    WSH_AST_NEGATE,
    /** Semantic context clone. */
    WSH_AST_SUBSHELL,
    /** Simple command with ordered items. */
    WSH_AST_SIMPLE,
    /** Braced command block. */
    WSH_AST_BLOCK,
    /** If command, including an optional if-not arm. */
    WSH_AST_IF,
    /** While command. */
    WSH_AST_WHILE,
    /** For command. */
    WSH_AST_FOR,
    /** Switch command. */
    WSH_AST_SWITCH,
    /** Switch case clause. */
    WSH_AST_CASE,
    /** Function definition or removal. */
    WSH_AST_FUNCTION,
    /** Assignment with name text and one value child. */
    WSH_AST_ASSIGNMENT,
    /** Ordered redirection item. */
    WSH_AST_REDIRECTION,
    /** Captured inert here-document body. */
    WSH_AST_HERE_BODY,
    /** Unquoted word argument. */
    WSH_AST_WORD,
    /** Apostrophe-quoted word argument. */
    WSH_AST_QUOTED_WORD,
    /** Parenthesized flat-list syntax. */
    WSH_AST_LIST,
    /** Variable expansion. */
    WSH_AST_VARIABLE,
    /** One subscript selection item. */
    WSH_AST_SUBSCRIPT,
    /** Variable count expansion. */
    WSH_AST_COUNT,
    /** Variable flattening expansion. */
    WSH_AST_FLATTEN,
    /** Inert command-substitution block. */
    WSH_AST_COMMAND_SUBSTITUTION,
    /** Inert read process-substitution block. */
    WSH_AST_PROCESS_READ,
    /** Inert write process-substitution block. */
    WSH_AST_PROCESS_WRITE,
    /** Inert duplex process-substitution block. */
    WSH_AST_PROCESS_DUPLEX,
    /** Explicit or free-caret concatenation. */
    WSH_AST_CONCAT
} wsh_ast_kind;

/** Opaque immutable AST node. */
typedef struct wsh_ast_node wsh_ast_node;

/** Opaque immutable parse-tree owner. */
typedef struct wsh_parse_tree wsh_parse_tree;

/**
 * Initialize conservative portable parser options.
 * @param out_options Required destination.
 */
void wsh_parser_options_init(wsh_parser_options *out_options);

/**
 * Lex one complete decoded source without performing an external effect.
 * @param options Parser options, or null for defaults.
 * @param source Decoded source borrowed for the call.
 * @param out_stream Receives the owned immutable stream on success.
 * @return WSH_OK or an argument/resource/internal error.
 */
wsh_result wsh_lex(
    const wsh_parser_options *options,
    const wsh_source *source,
    wsh_token_stream **out_stream);

/**
 * Destroy a token stream.
 * @param stream Owned stream; null is accepted.
 */
void wsh_token_stream_destroy(wsh_token_stream *stream);

/**
 * Return the lexical status.
 * @param stream Token-stream owner.
 * @return Complete, incomplete, or error.
 */
wsh_syntax_status wsh_token_stream_status(
    const wsh_token_stream *stream);

/**
 * Return the number of tokens, including EOF when lexing completed.
 * @param stream Token-stream owner.
 * @return Token count, or zero for null.
 */
size_t wsh_token_stream_count(const wsh_token_stream *stream);

/**
 * Inspect one token.
 * @param stream Token-stream owner.
 * @param index Zero-based token index.
 * @param out_token Receives a borrowed view.
 * @return WSH_OK or WSH_ERR_INVALID.
 */
wsh_result wsh_token_stream_at(
    const wsh_token_stream *stream,
    size_t index,
    wsh_token_view *out_token);

/**
 * Return retained lexical diagnostic count.
 * @param stream Token-stream owner.
 * @return Diagnostic count, or zero for null.
 */
size_t wsh_token_stream_diagnostic_count(
    const wsh_token_stream *stream);

/**
 * Inspect one lexical diagnostic.
 * @param stream Token-stream owner.
 * @param index Zero-based diagnostic index.
 * @param out_diagnostic Receives a borrowed view.
 * @return WSH_OK or WSH_ERR_INVALID.
 */
wsh_result wsh_token_stream_diagnostic_at(
    const wsh_token_stream *stream,
    size_t index,
    wsh_syntax_diagnostic_view *out_diagnostic);

/**
 * Parse one complete decoded source without evaluating it.
 * @param options Parser options, or null for defaults.
 * @param source Decoded source borrowed for the call.
 * @param out_tree Receives the owned immutable result on success.
 * @return WSH_OK or an argument/resource/internal error.
 */
wsh_result wsh_parse(
    const wsh_parser_options *options,
    const wsh_source *source,
    wsh_parse_tree **out_tree);

/**
 * Destroy a parse tree and every AST node it owns.
 * @param tree Owned tree; null is accepted.
 */
void wsh_parse_tree_destroy(wsh_parse_tree *tree);

/**
 * Return the parse status.
 * @param tree Parse-tree owner.
 * @return Complete, incomplete, or error.
 */
wsh_syntax_status wsh_parse_tree_status(const wsh_parse_tree *tree);

/**
 * Return the complete AST root.
 * @param tree Parse-tree owner.
 * @return Borrowed root only for a complete parse, otherwise null.
 */
const wsh_ast_node *wsh_parse_tree_root(const wsh_parse_tree *tree);

/**
 * Return retained parse diagnostic count.
 * @param tree Parse-tree owner.
 * @return Diagnostic count, or zero for null.
 */
size_t wsh_parse_tree_diagnostic_count(const wsh_parse_tree *tree);

/**
 * Inspect one parse diagnostic.
 * @param tree Parse-tree owner.
 * @param index Zero-based diagnostic index.
 * @param out_diagnostic Receives a borrowed view.
 * @return WSH_OK or WSH_ERR_INVALID.
 */
wsh_result wsh_parse_tree_diagnostic_at(
    const wsh_parse_tree *tree,
    size_t index,
    wsh_syntax_diagnostic_view *out_diagnostic);

/**
 * Return a stable token-kind name.
 * @param kind Token category.
 * @return Static ASCII name.
 */
const char *wsh_token_kind_name(wsh_token_kind kind);

/**
 * Return a stable AST-kind name.
 * @param kind AST category.
 * @return Static ASCII name.
 */
const char *wsh_ast_kind_name(wsh_ast_kind kind);

/**
 * Return an AST node's category.
 * @param node Borrowed AST node.
 * @return Node kind, or WSH_AST_INPUT for null.
 */
wsh_ast_kind wsh_ast_node_kind(const wsh_ast_node *node);

/**
 * Return an AST node's source span.
 * @param node Borrowed AST node.
 * @param out_span Receives the span.
 * @return WSH_OK or WSH_ERR_INVALID.
 */
wsh_result wsh_ast_node_span(
    const wsh_ast_node *node,
    wsh_source_span *out_span);

/**
 * Return optional primary node text.
 * @param node Borrowed AST node.
 * @return Borrowed text, empty when absent.
 */
wsh_string_view wsh_ast_node_text(const wsh_ast_node *node);

/**
 * Return optional auxiliary node text such as a descriptor decoration.
 * @param node Borrowed AST node.
 * @return Borrowed text, empty when absent.
 */
wsh_string_view wsh_ast_node_auxiliary(const wsh_ast_node *node);

/**
 * Return ordered child count.
 * @param node Borrowed AST node.
 * @return Child count, or zero for null.
 */
size_t wsh_ast_node_child_count(const wsh_ast_node *node);

/**
 * Inspect one ordered child.
 * @param node Borrowed AST node.
 * @param index Zero-based child index.
 * @return Borrowed child, or null when out of range.
 */
const wsh_ast_node *wsh_ast_node_child_at(
    const wsh_ast_node *node,
    size_t index);

/**
 * Format a complete AST as a deterministic escaped S-expression.
 * @param tree Complete parse-tree owner.
 * @param out_text Receives an owned immutable UTF-8 string.
 * @return WSH_OK or an argument/resource/internal error.
 */
wsh_result wsh_parse_tree_format(
    const wsh_parse_tree *tree,
    wsh_string **out_text);

#ifdef __cplusplus
}
#endif

#endif
