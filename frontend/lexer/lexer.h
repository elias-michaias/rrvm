#ifndef RR_LEXER_H
#define RR_LEXER_H

/*
 * rrvm/frontend/lexer/lexer.h
 *
 * Minimal line-oriented lexer helpers for the RRVM textual frontend.
 *
 * - Supports full-line comments that start with '#' (after optional leading
 *   whitespace). A comment is only recognized if the first non-space character
 *   on the line is '#'. Mid-line '#' characters (i.e. after tokens) are
 *   considered an error to help catch accidental trailing comments; the parser
 *   is expected to enforce that rule if desired. See lexer_tokenize_line
 *   return values for the error code for mid-line '#' usage.
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
 *  - -2    : line contains a '#' after non-space characters (mid-line comment)
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
 *  - -2    : line contains a '#' after non-space characters (mid-line comment),
 *           tokenization refused to accept mid-line comments to enforce the
 *           \"entire-line comment only\" rule.
 *
 * Behavior:
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