/*
 * rrvm/main.c
 *
 * CLI front-end for RRVM.
 *
 * Features:
 *  - Accepts a textual .rr program via --file <path> (or "-" for stdin).
 *  - Select backend at runtime: default interpreter; pass --tac to use TAC backend.
 *  - If no file is provided, preserves existing behavior and runs built-in sample
 *    programs in rrvm/programs/*.c.
 *
 * Notes:
 *  - Whole-line comments in .rr files must start with '#' as the first
 *    non-whitespace character. Mid-line '#' characters are rejected by the
 *    lexer and will raise a parse error.
 *  - When running with the TAC backend on a parsed file, a TAC Prolog dump is
 *    written to "opt/tmp/raw/parsed.pl".
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

/* VM and backends */
#include "vm/vm.h"
#include "backend/interpreter/interpreter.h"
#include "backend/tac/tac.h"

/* Parser for .rr textual input */
#include "frontend/parser/parser.h"

/* Built-in sample programs removed. The CLI now requires --file <path> (or '-' for stdin). */

static void print_usage(const char *prog) {
    fprintf(stderr,
        "Usage: %s [--file <path>|-] [--tac] [--help]\n"
        "  --file <path>   Parse and run the given .rr file. Use '-' to read stdin.\n"
        "  --tac           Use TAC backend (default: interpreter).\n"
        "  --help          Show this help message.\n\n"
        "If --file is not provided the built-in sample programs are executed (same\n"
        "behaviour as before).\n",
        prog ?: "rrvm"
    );
}

int main(int argc, char **argv) {
    bool use_tac = false;
    const char *file_path = NULL;

    /* Simple argument parsing (no getopt to keep portability) */
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--tac") == 0) {
            use_tac = true;
        } else if ((strcmp(argv[i], "--file") == 0 || strcmp(argv[i], "-f") == 0)) {
            if (i + 1 >= argc) {
                fprintf(stderr, "error: --file requires an argument\n");
                print_usage(argv[0]);
                return 2;
            }
            file_path = argv[++i];
        } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            print_usage(argv[0]);
            return 0;
        } else {
            /* treat lone positional as file if not specified */
            if (!file_path) file_path = argv[i];
            else {
                fprintf(stderr, "error: unknown argument: %s\n", argv[i]);
                print_usage(argv[0]);
                return 2;
            }
        }
    }

    const Backend *backend = use_tac ? &__TAC : &__INTERPRETER;

    if (file_path) {
        /* Parse the provided .rr file (or stdin if "-") */
        VM vm_parsed;
        char *err = NULL;
        int r = parse_rr_file_to_vm(file_path, &vm_parsed, &err);
        if (r != 0) {
            if (err) {
                fprintf(stderr, "parse error: %s\n", err);
                free(err);
            } else {
                fprintf(stderr, "parse error: unknown\n");
            }
            return 1;
        }

        /* Run the parsed VM with the selected backend */
        run_vm(&vm_parsed, backend);

        /* If TAC backend, dump TAC and write prolog file for post-processing */
        if (use_tac) {
            tac_prog *prog = tac_get_prog(&vm_parsed);
            if (prog) {
                tac_dump(prog);
                /* write to default path opt/tmp/raw/parsed.pl */
                tac_dump_file(prog, "opt/tmp/parsed.pl");
            }
        }

        /* finalize backend (free backend-specific user_data) */
        if (backend && backend->finalize) backend->finalize(&vm_parsed, 0);

        /* free program code allocated by parser */
        parser_free_vm_code(&vm_parsed);

        return 0;
    }

    /* No file provided: require explicit --file argument. */
    fprintf(stderr, "error: no input file specified. Use --file <path> or '-' for stdin\n");
    print_usage(argv[0]);
    return 2;
}