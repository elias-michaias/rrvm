#ifndef RR_LEXER_H
#define RR_LEXER_H

/*
 * rrvm/frontend/lexer/lexer.h
 *
 * Minimal line-oriented lexer helpers for the RRVM textual frontend.
 *
 * - Supports comments that start with '#' anywhere on the line. If the first
 *   non-space character is '#', the whole line is considered a comment and
 *   tokenization yields zero tokens. If a '#' appears after code on the line,
 *   the '#' and everything after it is treated as a trailing comment and is
 *   ignored; tokenization returns only the tokens appearing before the '#'.
 *
 * - Tokenization is whitespace-separated. This header provides a small helper
 *   that splits a single line into tokens (char* strings) which the caller
 *   must free via `lexer_free_tokens`.
 *
 * Notes on memory ownership:
 *  - The token array returned by `lexer_tokenize_line` is a NULL-terminated
 *    array of heap-allocated NUL-terminated strings. The caller must call
 *    `lexer_free_tokens` to release both the strings and the array pointer.
 *
 * Error codes (lexer_tokenize_line):
 *  - >= 0 : number of tokens returned (and *out_tokens will be set)
 *  - -1    : allocation failure
 *
 * The API intentionally keeps the lexer very small and line-oriented to make
 * it easy to integrate into a line-based parser. It does not produce token
 * positions; the parser can track line numbers itself.
 */

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Return non-zero if `line` (a NUL-terminated C string, newline already stripped)
 * is an entire-line comment. Leading whitespace is allowed. Empty lines are
 * NOT considered comment lines (the caller can treat them specially).
 */
int lexer_is_comment_line(const char *line);

/* Tokenize a single input line into whitespace-separated tokens.
 *
 * Parameters:
 *  - line: NUL-terminated input line (should not contain trailing newline).
 *  - out_tokens: out param that will be set to point to a newly-allocated
 *                NULL-terminated array of NUL-terminated C strings on
 *                success (caller MUST free via lexer_free_tokens).
 *
 * Return values:
 *  - >= 0 : token count (also the length of the array without the terminating NULL)
 *  - -1    : allocation failure (out_tokens is left unmodified)
 *
 * Behavior:
 *  - A '#' character begins a comment: if the first non-space character is '#',
 *    the whole line is a comment and tokenization returns zero tokens. If a
 *    '#' appears after code, the '#' and everything after it is ignored and
 *    tokens before it are returned (i.e. trailing comments are supported).
 *  - Consecutive whitespace (space + tab) is treated as a single separator.
 *  - Empty lines produce token count 0 and `*out_tokens` will point to a
 *    valid pointer (an array with a single NULL entry).
 *  - Leading/trailing whitespace is ignored.
 */
int lexer_tokenize_line(const char *line, char ***out_tokens);

/* Free tokens produced by `lexer_tokenize_line`.
 * Accepts NULL (no-op).
 */
void lexer_free_tokens(char **tokens);

#ifdef __cplusplus
}
#endif

#endif /* RR_LEXER_H */