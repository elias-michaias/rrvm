/*
 * rrvm/frontend/lexer/lexer.c
 *
 * Simple line-oriented lexer implementation.
 *
 * See rrvm/frontend/lexer/lexer.h for API semantics.
 *
 * Behavior summary:
 *  - `lexer_is_comment_line(line)` returns non-zero if the first non-space
 *    character in `line` is '#'. Empty lines are NOT considered comments.
 *
 *  - `lexer_tokenize_line(line, &out_tokens)` splits `line` on whitespace
 *    (space and tab and other isspace chars except newline), returning a
 *    NULL-terminated array of strings. The caller must free the returned
 *    array and strings using `lexer_free_tokens`.
 *
 *    Special cases:
 *      * Empty line -> returns 0 and `*out_tokens` is set to an array with
 *        a single NULL element.
 *      * Entire-line comment (first non-space char is '#') -> treated like
 *        an empty line (returns 0 and `*out_tokens` points to array with NULL).
 *      * A '#' that appears after some non-space characters is treated as a
 *        trailing comment: the '#' and everything after it is ignored and
 *        tokenization returns only the tokens before the '#'.
 *
 *    On allocation failure returns -1.
 *
 *  - `lexer_free_tokens` frees memory allocated by `lexer_tokenize_line`.
 */
 
#include "lexer.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
 
/* Helper: duplicate a substring of length `len`. Returns malloc'ed NUL-terminated
 * string or NULL on allocation failure.
 */
static char *lex_dup_substr(const char *s, size_t len) {
    char *d = (char *)malloc(len + 1);
    if (!d) return NULL;
    if (len) memcpy(d, s, len);
    d[len] = '\0';
    return d;
}
 
int lexer_is_comment_line(const char *line) {
    if (!line) return 0;
    const char *p = line;
    /* skip leading whitespace */
    while (*p && isspace((unsigned char)*p)) ++p;
    /* empty line is not a comment per header */
    if (*p == '\0') return 0;
    return (*p == '#') ? 1 : 0;
}
 
int lexer_tokenize_line(const char *line, char ***out_tokens) {
    if (!out_tokens) return -1; /* invalid caller usage */
 
    /* Handle NULL input as empty line */
    if (!line) {
        char **arr = (char **)malloc(sizeof(char *));
        if (!arr) return -1;
        arr[0] = NULL;
        *out_tokens = arr;
        return 0;
    }
 
    const char *p = line;
    /* detect leading whitespace and first non-space char */
    const char *q = p;
    while (*q && isspace((unsigned char)*q)) ++q;
    if (*q == '\0') {
        /* empty line: return zero tokens and a valid NULL-terminated array */
        char **arr = (char **)malloc(sizeof(char *));
        if (!arr) return -1;
        arr[0] = NULL;
        *out_tokens = arr;
        return 0;
    }
    if (*q == '#') {
        /* entire-line comment: treat like empty line */
        char **arr = (char **)malloc(sizeof(char *));
        if (!arr) return -1;
        arr[0] = NULL;
        *out_tokens = arr;
        return 0;
    }
 
    /* Tokenize: split on whitespace; treat '#' as start of trailing comment. */
    size_t capacity = 8;
    size_t count = 0;
    char **tokens = (char **)malloc(capacity * sizeof(char *));
    if (!tokens) return -1;
 
    while (*p) {
        /* skip whitespace */
        while (*p && isspace((unsigned char)*p)) ++p;
        if (!*p) break;
 
        /* If we encounter a '#', treat the remainder of the line as a comment */
        if (*p == '#') break;
 
        /* start of token */
        const char *start = p;
        int saw_hash = 0;
        while (*p && !isspace((unsigned char)*p)) {
            if (*p == '#') { saw_hash = 1; break; }
            ++p;
        }
        size_t len = (size_t)(p - start);
        if (len == 0) {
            /* if we hit '#' immediately after whitespace, stop processing */
            if (saw_hash) break;
            else continue;
        }
        char *tok = lex_dup_substr(start, len);
        if (!tok) {
            for (size_t i = 0; i < count; ++i) free(tokens[i]);
            free(tokens);
            return -1;
        }
 
        if (count + 1 >= capacity) {
            size_t newcap = capacity * 2;
            char **tmp = (char **)realloc(tokens, newcap * sizeof(char *));
            if (!tmp) {
                /* allocation failure: free everything */
                free(tok);
                for (size_t i = 0; i < count; ++i) free(tokens[i]);
                free(tokens);
                return -1;
            }
            tokens = tmp;
            capacity = newcap;
        }
        tokens[count++] = tok;
 
        /* if token scan stopped at '#', treat rest of line as trailing comment */
        if (saw_hash) break;
    }
 
    /* ensure NULL-terminated array */
    if (count + 1 > capacity) {
        char **tmp = (char **)realloc(tokens, (count + 1) * sizeof(char *));
        if (!tmp) {
            for (size_t i = 0; i < count; ++i) free(tokens[i]);
            free(tokens);
            return -1;
        }
        tokens = tmp;
    }
    tokens[count] = NULL;
    *out_tokens = tokens;
    return (int)count;
}
 
void lexer_free_tokens(char **tokens) {
    if (!tokens) return;
    for (size_t i = 0; tokens[i] != NULL; ++i) {
        free(tokens[i]);
    }
    free(tokens);
}