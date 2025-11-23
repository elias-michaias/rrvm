#ifndef RR_PARSER_H
#define RR_PARSER_H

/*
 * rrvm/frontend/parser/parser.h
 *
 * Parser interface for the RRVM textual frontend (.rr files).
 *
 * Responsibilities:
 *  - Parse a textual .rr program (file or in-memory string) and emit a VM
 *    instance whose `code` buffer is heap-allocated. The caller owns and must
 *    free `out_vm->code` when finished (call free()).
 *  - Report parsing errors with human-friendly messages including line info
 *    via `*err_msg` when non-zero is returned.
 *
 * Notes:
 *  - The parser accepts comments beginning with '#' anywhere on the line.
 *    If the first non-space character is '#', the whole line is treated as a
 *    comment and no tokens are produced. If a '#' appears after code on the
 *    same line, the '#' and the remainder of the line are treated as a
 *    trailing comment and ignored; tokenization returns only the tokens that
 *    appear before the '#'.
 *  - On success the parser initializes `out_vm` similarly to how the C
 *    emit macros produce a VM: `out_vm->code` points to an allocated `word[]`
 *    and `out_vm->code_len` is set. The rest of the VM fields (ip/sp etc.)
 *    will be initialized by `run_vm`.
 *
 * Error handling:
 *  - The parsing functions return 0 on success.
 *  - On error they return non-zero and set `*err_msg` to a malloc'ed NUL
 *    terminated string describing the error. The caller must free(*err_msg).
 */

#include "../vm/vm.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Parse the .rr source located at `path`.
 *
 * Parameters:
 *  - path: path to source file. If path is "-" the parser reads from stdin.
 *  - out_vm: pointer to a VM struct to populate; `out_vm->code` will be set
 *            to a malloc'ed buffer on success.
 *  - err_msg: out param; on error the function sets *err_msg to a newly
 *             allocated error string describing the failure. On success
 *             *err_msg is left unchanged (or set to NULL).
 *
 * Return:
 *  - 0 on success
 *  - non-zero on error (caller must free *err_msg)
 */
int parse_rr_file_to_vm(const char *path, VM *out_vm, char **err_msg);

/*
 * Parse from an in-memory NUL-terminated string.
 *
 * Same semantics as parse_rr_file_to_vm except the input is provided in `src`.
 */
int parse_rr_string_to_vm(const char *src, VM *out_vm, char **err_msg);

/*
 * Utility: free resources produced by parse_* functions on the VM.
 * This currently only frees `out_vm->code` if non-NULL and zeroes fields
 * that are tied to that buffer. The caller may still need to call backend
 * finalize hooks if applicable.
 *
 * Note: This helper does not free any VM.user_data or backend-specific
 * allocations; it's intended to free the program code buffer allocated by
 * the parser.
 */
void parser_free_vm_code(VM *vm);

#ifdef __cplusplus
}
#endif

#endif /* RR_PARSER_H */
