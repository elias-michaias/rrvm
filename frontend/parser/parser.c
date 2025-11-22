/*
 * rrvm/frontend/parser/parser.c
 *
 * Textual .rr parser implementation.
 *
 * Supports a modest instruction subset:
 *   push <type> <imm>
 *   set <type> <imm>
 *   add sub mul div rem
 *   move <imm>
 *   load store print
 *   deref refer where offset <imm> index
 *   func <name>
 *   call <name>
 *   ret | return
 *   if else end
 *   label <name>   (also supports `name:`)
 *   while <label>
 *   halt
 *
 * Comments:
 *   Trailing and full-line comments beginning with '#' are supported.
 *   If the first non-space character on the line is '#', the entire line
 *   is treated as a comment and no tokens are produced. If a '#' appears
 *   after code on the same line, the '#' and the remainder of the line are
 *   treated as a trailing comment and ignored; tokenization returns only the
 *   tokens that appear before the '#'.
 *
 * The parser emits the same VM opcode layout expected by vm.h. The caller is
 * responsible for freeing the produced `out_vm->code` buffer (use free()).
 */

#include "parser.h"
#include "../lexer/lexer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h> /* strcasecmp */
#include <ctype.h>
#include <stdarg.h>
#include <errno.h>

/* Prototype for the local strdup replacement so it can be used before its
 * definition appears later in the file. */
static char *xstrdup(const char *s);

static void set_error_msg(char **err, const char *fmt, ...) {
    if (!err) return;
    va_list ap;
    va_start(ap, fmt);
    /* attempt to allocate a reasonably sized buffer */
    char tmp[512];
    int n = vsnprintf(tmp, sizeof(tmp), fmt, ap);
    va_end(ap);

    if (n < (int)sizeof(tmp)) {
        *err = (char*)malloc((size_t)n + 1);
        if (*err) strcpy(*err, tmp);
        return;
    }

    /* larger: allocate needed size */
    va_list ap2;
    va_start(ap2, fmt);
    char *buf = (char*)malloc((size_t)n + 1);
    if (!buf) {
        *err = NULL;
        va_end(ap2);
        return;
    }
    vsnprintf(buf, (size_t)n + 1, fmt, ap2);
    va_end(ap2);
    *err = buf;
}

/* --- dynamic program buffer helpers --- */
static int code_grow_needed(size_t len, size_t *cap) {
    if (len + 1 > *cap) {
        size_t nc = (*cap ? *cap * 2 : 64);
        while (nc < len + 1) nc *= 2;
        *cap = nc;
        return 1;
    }
    return 0;
}

static int code_ensure(word **codep, size_t len, size_t *cap) {
    if (code_grow_needed(len, cap)) {
        word *n = (word*)realloc(*codep, (*cap) * sizeof(word));
        if (!n) return -1;
        *codep = n;
    }
    return 0;
}

/* --- symbol tables and patches --- */

typedef struct {
    char *name;
    size_t pos;    /* vm code position where label is defined */
    int defined;
} LabelEntry;

typedef struct {
    LabelEntry *arr;
    size_t count, cap;
} LabelTable;

static void label_table_free(LabelTable *t) {
    if (!t) return;
    for (size_t i = 0; i < t->count; ++i) free(t->arr[i].name);
    free(t->arr);
    t->arr = NULL;
    t->count = t->cap = 0;
}

static LabelEntry* label_table_find(LabelTable *t, const char *name) {
    if (!t || !t->arr) return NULL;
    for (size_t i = 0; i < t->count; ++i) {
        if (strcmp(t->arr[i].name, name) == 0) return &t->arr[i];
    }
    return NULL;
}

static int label_table_add(LabelTable *t, const char *name, size_t pos, int defined, char **err) {
    if (label_table_find(t, name)) {
        /* If already defined, and defined==1 -> duplicate label error */
        LabelEntry *e = label_table_find(t, name);
        if (e->defined && defined) {
            set_error_msg(err, "label '%s' redefined", name);
            return -1;
        }
        /* update if we're now defining it */
        if (defined) {
            e->pos = pos;
            e->defined = 1;
        }
        return 0;
    }
    /* add new */
    if (t->count + 1 > t->cap) {
        size_t nc = t->cap ? t->cap * 2 : 16;
        LabelEntry *n = (LabelEntry*)realloc(t->arr, nc * sizeof(LabelEntry));
        if (!n) { set_error_msg(err, "out of memory"); return -1; }
        t->arr = n;
        t->cap = nc;
    }
    t->arr[t->count].name = xstrdup(name);
    t->arr[t->count].pos = pos;
    t->arr[t->count].defined = defined ? 1 : 0;
    t->count++;
    return 0;
}

/* functions */

typedef struct {
    char *name;
    int index;
    int defined;
} FuncEntry;

typedef struct {
    FuncEntry *arr;
    size_t count, cap;
    int next_index;
} FuncTable;

static void func_table_init(FuncTable *t) { t->arr = NULL; t->count = t->cap = 0; t->next_index = 0; }
static void func_table_free(FuncTable *t) {
    if (!t) return;
    for (size_t i = 0; i < t->count; ++i) free(t->arr[i].name);
    free(t->arr);
    t->arr = NULL;
    t->count = t->cap = 0;
}

static FuncEntry* func_table_find(FuncTable *t, const char *name) {
    if (!t || !t->arr) return NULL;
    for (size_t i = 0; i < t->count; ++i) if (strcmp(t->arr[i].name, name) == 0) return &t->arr[i];
    return NULL;
}

static int func_table_get_or_create(FuncTable *t, const char *name, int *out_idx, char **err) {
    FuncEntry *e = func_table_find(t, name);
    if (e) { *out_idx = e->index; return 0; }
    /* create new */
    if (t->count + 1 > t->cap) {
        size_t nc = t->cap ? t->cap * 2 : 16;
        FuncEntry *n = (FuncEntry*)realloc(t->arr, nc * sizeof(FuncEntry));
        if (!n) { set_error_msg(err, "out of memory"); return -1; }
        t->arr = n;
        t->cap = nc;
    }
    int idx = t->next_index++;
    t->arr[t->count].name = xstrdup(name);
    t->arr[t->count].index = idx;
    t->arr[t->count].defined = 0;
    *out_idx = idx;
    t->count++;
    return 0;
}

static int func_table_mark_defined(FuncTable *t, const char *name, char **err) {
    FuncEntry *e = func_table_find(t, name);
    if (!e) {
        /* create and mark defined */
        if (t->count + 1 > t->cap) {
            size_t nc = t->cap ? t->cap * 2 : 16;
            FuncEntry *n = (FuncEntry*)realloc(t->arr, nc * sizeof(FuncEntry));
            if (!n) { set_error_msg(err, "out of memory"); return -1; }
            t->arr = n;
            t->cap = nc;
        }
        int idx = t->next_index++;
        t->arr[t->count].name = xstrdup(name);
        t->arr[t->count].index = idx;
        t->arr[t->count].defined = 1;
        t->count++;
        return 0;
    }
    if (e->defined) { set_error_msg(err, "function '%s' redefined", name); return -1; }
    e->defined = 1;
    return 0;
}

/* while patch entries: hold places where OP_WHILE's immediate must be backpatched */
typedef struct {
    char *label;
    size_t imm_pos; /* index in code array where placeholder immediate was emitted */
} WhilePatch;

typedef struct {
    WhilePatch *arr;
    size_t count, cap;
} WhilePatchTable;

static void whilepatch_free(WhilePatchTable *t) {
    if (!t) return;
    for (size_t i = 0; i < t->count; ++i) free(t->arr[i].label);
    free(t->arr);
    t->arr = NULL;
    t->count = t->cap = 0;
}

static int whilepatch_add(WhilePatchTable *t, const char *label, size_t pos, char **err) {
    if (t->count + 1 > t->cap) {
        size_t nc = t->cap ? t->cap * 2 : 16;
        WhilePatch *n = (WhilePatch*)realloc(t->arr, nc * sizeof(WhilePatch));
        if (!n) { set_error_msg(err, "out of memory"); return -1; }
        t->arr = n;
        t->cap = nc;
    }
    t->arr[t->count].label = xstrdup(label);
    t->arr[t->count].imm_pos = pos;
    t->count++;
    return 0;
}

/* --- helpers --- */

static int parse_int64(const char *s, word *out) {
    if (!s) return -1;
    char *end = NULL;
    errno = 0;
    long long v = strtoll(s, &end, 0);
    if (end == s || errno) return -1;
    *out = (word)v;
    return 0;
}

/* Parse an f32 immediate which may be either:
 *  - a hex bit-pattern like 0x3fc00000 (raw IEEE-754 bits), or
 *  - a numeric literal like 1.5 (decimal or C hex-float), which is parsed
 *    as a floating value and then bit-cast into the 32-bit pattern.
 *
 * The resulting 32-bit pattern is stored into the lower 32 bits of the VM word.
 */
static int parse_f32_or_bits(const char *s, word *out) {
    if (!s) return -1;
    char *end = NULL;
    errno = 0;

    /* If the caller provided an explicit hex-style immediate, parse as unsigned
     * to allow the high bit to be set (raw bit-pattern).
     */
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        unsigned long long v = strtoull(s, &end, 0);
        if (end == s || errno) return -1;
        uint32_t bits = (uint32_t)v;
        *out = (word)bits;
        return 0;
    }

    /* Otherwise parse as a floating-point literal (strtod accepts decimal and
     * C hex-float forms such as 0x1.8p+1) and downcast to float */
    errno = 0;
    char *end2 = NULL;
    double dv = strtod(s, &end2);
    if (end2 == s || errno) return -1;
    float fv = (float)dv;
    union { uint32_t u; float f; } u;
    u.f = fv;
    *out = (word)u.u;
    return 0;
}

/* Parse an f64 immediate which may be either:
 *  - a hex bit-pattern like 0x3ff8000000000000 (raw IEEE-754 bits), or
 *  - a numeric literal like 1.5 (decimal or C hex-float), which is parsed
 *    as a floating value and bit-cast into the 64-bit pattern.
 */
static int parse_f64_or_bits(const char *s, word *out) {
    if (!s) return -1;
    char *end = NULL;
    errno = 0;

    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        unsigned long long v = strtoull(s, &end, 0);
        if (end == s || errno) return -1;
        uint64_t bits = (uint64_t)v;
        *out = (word)bits;
        return 0;
    }

    errno = 0;
    char *end2 = NULL;
    double dv = strtod(s, &end2);
    if (end2 == s || errno) return -1;
    union { uint64_t u; double d; } u;
    u.d = dv;
    *out = (word)u.u;
    return 0;
}

static int type_tag_from_str(const char *s) {
    if (!s) return TYPE_UNKNOWN;
    if (strcasecmp(s, "i8") == 0) return TYPE_I8;
    if (strcasecmp(s, "u8") == 0) return TYPE_U8;
    if (strcasecmp(s, "i16") == 0) return TYPE_I16;
    if (strcasecmp(s, "u16") == 0) return TYPE_U16;
    if (strcasecmp(s, "i32") == 0) return TYPE_I32;
    if (strcasecmp(s, "u32") == 0) return TYPE_U32;
    if (strcasecmp(s, "i64") == 0) return TYPE_I64;
    if (strcasecmp(s, "u64") == 0) return TYPE_U64;
    if (strcasecmp(s, "f32") == 0) return TYPE_F32;
    if (strcasecmp(s, "f64") == 0) return TYPE_F64;
    if (strcasecmp(s, "bool") == 0) return TYPE_BOOL;
    if (strcasecmp(s, "ptr") == 0) return TYPE_PTR;
    if (strcasecmp(s, "void") == 0) return TYPE_VOID;
    return TYPE_UNKNOWN;
}

static char *lowerdup(const char *s) {
    if (!s) return NULL;
    size_t n = strlen(s);
    char *d = (char*)malloc(n + 1);
    if (!d) return NULL;
    for (size_t i = 0; i < n; ++i) d[i] = (char)tolower((unsigned char)s[i]);
    d[n] = '\0';
    return d;
}

/* safe strdup wrapper implemented without strdup to avoid POSIX-only dependency */
/* xstrdup implemented at end of file to avoid duplicate definitions */

/* free helpers for dynamically allocated tables */
static void free_labeltable_and_patches(LabelTable *lt, WhilePatchTable *wpt) {
    if (lt) label_table_free(lt);
    if (wpt) whilepatch_free(wpt);
}

/* --- main parser implementation --- */

int parse_rr_string_to_vm(const char *src, VM *out_vm, char **err_msg) {
    if (!src || !out_vm) {
        set_error_msg(err_msg, "internal: invalid arguments");
        return -1;
    }

    /* We'll emulate line-based reading by copying src and splitting on newlines */
    char *buf = xstrdup(src);
    if (!buf) { set_error_msg(err_msg, "out of memory"); return -1; }

    word *code = NULL;
    size_t code_len = 0, code_cap = 0;

    LabelTable labels = {0};
    FuncTable funcs; func_table_init(&funcs);
    WhilePatchTable wpatches = {0};

    char *line = strtok(buf, "\n");
    size_t lineno = 0;

    while (line) {
        lineno++;
        /* Strip trailing CR if present (handle CRLF) */
        size_t ln = strlen(line);
        if (ln && line[ln-1] == '\r') line[--ln] = '\0';

        /* Check for whole-line comment */
        if (lexer_is_comment_line(line)) {
            line = strtok(NULL, "\n");
            continue;
        }

        /* Tokenize the line */
        char **tokens = NULL;
        int ntok = lexer_tokenize_line(line, &tokens);
        if (ntok < 0) {
            free(buf);
            free_labeltable_and_patches(&labels, &wpatches);
            func_table_free(&funcs);
            set_error_msg(err_msg, "line %zu: tokenization error", lineno);
            return -1;
        }
        if (ntok == 0) { lexer_free_tokens(tokens); line = strtok(NULL, "\n"); continue; }

        /* helper to append opcode words */
        #define EMIT0(op) do { if (code_ensure(&code, code_len + 1, &code_cap) < 0) { set_error_msg(err_msg, "out of memory"); goto fail; } code[code_len++] = (word)(op); } while(0)
        #define EMIT1(op,x1) do { if (code_ensure(&code, code_len + 2, &code_cap) < 0) { set_error_msg(err_msg, "out of memory"); goto fail; } code[code_len++] = (word)(op); code[code_len++] = (word)(x1); } while(0)
        #define EMIT2(op,x1,x2) do { if (code_ensure(&code, code_len + 3, &code_cap) < 0) { set_error_msg(err_msg, "out of memory"); goto fail; } code[code_len++] = (word)(op); code[code_len++] = (word)(x1); code[code_len++] = (word)(x2); } while(0)

        /* Recognize label of form 'name:' as first token */
        int consumed_tokens = 0;
        char *first = tokens[0];
        size_t flen = strlen(first);
        if (flen > 1 && first[flen-1] == ':') {
            /* label definition */
            char namebuf[256];
            if (flen-1 >= sizeof(namebuf)) {
                set_error_msg(err_msg, "line %zu: label name too long", lineno);
                lexer_free_tokens(tokens); goto fail;
            }
            memcpy(namebuf, first, flen-1);
            namebuf[flen-1] = '\0';
            if (label_table_add(&labels, namebuf, code_len, 1, err_msg) < 0) {
                lexer_free_tokens(tokens); goto fail;
            }
            /* backpatch any wpatches that refer to this label */
            for (size_t pi = 0; pi < wpatches.count; ++pi) {
                if (strcmp(wpatches.arr[pi].label, namebuf) == 0) {
                    size_t immpos = wpatches.arr[pi].imm_pos;
                    code[immpos] = (word)code_len;
                    free(wpatches.arr[pi].label);
                    /* remove this entry by swapping last in */
                    wpatches.arr[pi] = wpatches.arr[wpatches.count - 1];
                    wpatches.count--;
                    pi--; /* re-evaluate current index */
                }
            }
        consumed_tokens = 1;
        /* maybe there are tokens after a label on the same line? For simplicity disallow. */
        if (ntok > 1) {
            set_error_msg(err_msg, "line %zu: tokens after label on same line are not allowed", lineno);
            lexer_free_tokens(tokens); goto fail;
        }
        lexer_free_tokens(tokens);
        line = strtok(NULL, "\n");
        continue;
    }

        /* interpret tokens as instructions */
        char *kwlow = lowerdup(tokens[0]);
        if (!kwlow) { set_error_msg(err_msg, "out of memory"); lexer_free_tokens(tokens); goto fail; }

        if (strcasecmp(kwlow, "push") == 0) {
            if (ntok != 3) { set_error_msg(err_msg, "line %zu: push expects: push <type> <imm>", lineno); free(kwlow); lexer_free_tokens(tokens); goto fail; }
            int t = type_tag_from_str(tokens[1]);
            word imm;
            /* Accept either raw hex bit-patterns or human-friendly numeric literals
             * for float types. For non-float types, fall back to integer parsing.
             */
            if (t == TYPE_F32) {
                if (parse_f32_or_bits(tokens[2], &imm) < 0) { set_error_msg(err_msg, "line %zu: invalid f32 immediate '%s'", lineno, tokens[2]); free(kwlow); lexer_free_tokens(tokens); goto fail; }
            } else if (t == TYPE_F64) {
                if (parse_f64_or_bits(tokens[2], &imm) < 0) { set_error_msg(err_msg, "line %zu: invalid f64 immediate '%s'", lineno, tokens[2]); free(kwlow); lexer_free_tokens(tokens); goto fail; }
            } else {
                if (parse_int64(tokens[2], &imm) < 0) { set_error_msg(err_msg, "line %zu: invalid immediate '%s'", lineno, tokens[2]); free(kwlow); lexer_free_tokens(tokens); goto fail; }
            }
            EMIT2(OP_PUSH, (word)t, imm);
        } else if (strcasecmp(kwlow, "set") == 0) {
            if (ntok != 3) { set_error_msg(err_msg, "line %zu: set expects: set <type> <imm>", lineno); free(kwlow); lexer_free_tokens(tokens); goto fail; }
            int t = type_tag_from_str(tokens[1]);
            word imm;
            if (t == TYPE_F32) {
                if (parse_f32_or_bits(tokens[2], &imm) < 0) { set_error_msg(err_msg, "line %zu: invalid f32 immediate '%s'", lineno, tokens[2]); free(kwlow); lexer_free_tokens(tokens); goto fail; }
            } else if (t == TYPE_F64) {
                if (parse_f64_or_bits(tokens[2], &imm) < 0) { set_error_msg(err_msg, "line %zu: invalid f64 immediate '%s'", lineno, tokens[2]); free(kwlow); lexer_free_tokens(tokens); goto fail; }
            } else {
                if (parse_int64(tokens[2], &imm) < 0) { set_error_msg(err_msg, "line %zu: invalid immediate '%s'", lineno, tokens[2]); free(kwlow); lexer_free_tokens(tokens); goto fail; }
            }
            EMIT2(OP_SET, (word)t, imm);
        } else if (strcasecmp(kwlow, "add") == 0) { EMIT0(OP_ADD);
        } else if (strcasecmp(kwlow, "sub") == 0) { EMIT0(OP_SUB);
        } else if (strcasecmp(kwlow, "mul") == 0) { EMIT0(OP_MUL);
        } else if (strcasecmp(kwlow, "div") == 0) { EMIT0(OP_DIV);
        } else if (strcasecmp(kwlow, "rem") == 0) { EMIT0(OP_REM);
        } else if (strcasecmp(kwlow, "move") == 0) {
            if (ntok != 2) { set_error_msg(err_msg, "line %zu: move expects a signed immediate", lineno); free(kwlow); lexer_free_tokens(tokens); goto fail; }
            word imm; if (parse_int64(tokens[1], &imm) < 0) { set_error_msg(err_msg, "line %zu: invalid immediate '%s'", lineno, tokens[1]); free(kwlow); lexer_free_tokens(tokens); goto fail; }
            EMIT1(OP_MOVE, imm);
        } else if (strcasecmp(kwlow, "load") == 0) { EMIT0(OP_LOAD);
        } else if (strcasecmp(kwlow, "store") == 0) { EMIT0(OP_STORE);
        } else if (strcasecmp(kwlow, "print") == 0) { EMIT0(OP_PRINT);
        } else if (strcasecmp(kwlow, "printchar") == 0 || strcasecmp(kwlow, "print_char") == 0) { EMIT0(OP_PRINTCHAR);
        } else if (strcasecmp(kwlow, "deref") == 0) { EMIT0(OP_DEREF);
        } else if (strcasecmp(kwlow, "refer") == 0) { EMIT0(OP_REFER);
        } else if (strcasecmp(kwlow, "where") == 0) { EMIT0(OP_WHERE);
        } else if (strcasecmp(kwlow, "offset") == 0) {
            if (ntok != 2) { set_error_msg(err_msg, "line %zu: offset expects an immediate", lineno); free(kwlow); lexer_free_tokens(tokens); goto fail; }
            word imm; if (parse_int64(tokens[1], &imm) < 0) { set_error_msg(err_msg, "line %zu: invalid immediate '%s'", lineno, tokens[1]); free(kwlow); lexer_free_tokens(tokens); goto fail; }
            EMIT1(OP_OFFSET, imm);
        } else if (strcasecmp(kwlow, "index") == 0) { EMIT0(OP_INDEX);
        } else if (strcasecmp(kwlow, "func") == 0) {
            if (ntok != 2) { set_error_msg(err_msg, "line %zu: func expects: func <name>", lineno); free(kwlow); lexer_free_tokens(tokens); goto fail; }
            /* mark function as defined at current position by name */
            if (func_table_mark_defined(&funcs, tokens[1], err_msg) < 0) { free(kwlow); lexer_free_tokens(tokens); goto fail; }
            /* ensure a function index exists for the name; get it */
            int idx;
            if (func_table_get_or_create(&funcs, tokens[1], &idx, err_msg) < 0) { free(kwlow); lexer_free_tokens(tokens); goto fail; }
            /* emit OP_FUNCTION <index> */
            EMIT1(OP_FUNCTION, (word)idx);
        } else if (strcasecmp(kwlow, "call") == 0) {
            if (ntok != 2) { set_error_msg(err_msg, "line %zu: call expects: call <name>", lineno); free(kwlow); lexer_free_tokens(tokens); goto fail; }
            int idx;
            if (func_table_get_or_create(&funcs, tokens[1], &idx, err_msg) < 0) { free(kwlow); lexer_free_tokens(tokens); goto fail; }
            EMIT1(OP_CALL, (word)idx);
        } else if (strcasecmp(kwlow, "ret") == 0 || strcasecmp(kwlow, "return") == 0) { EMIT0(OP_RETURN);
        } else if (strcasecmp(kwlow, "if") == 0) { EMIT0(OP_IF);
        } else if (strcasecmp(kwlow, "else") == 0) { EMIT0(OP_ELSE);
        } else if (strcasecmp(kwlow, "end") == 0) { EMIT0(OP_ENDBLOCK);
        } else if (strcasecmp(kwlow, "while") == 0) {
            if (ntok != 2) { set_error_msg(err_msg, "line %zu: while expects: while <label>", lineno); free(kwlow); lexer_free_tokens(tokens); goto fail; }
            /* emit OP_WHILE <cond_ip> where cond_ip is label position; allow forward refs */
            /* append OP_WHILE then placeholder */
            size_t placeholder_pos;
            if (code_ensure(&code, code_len + 2, &code_cap) < 0) { set_error_msg(err_msg, "out of memory"); free(kwlow); lexer_free_tokens(tokens); goto fail; }
            code[code_len++] = (word)OP_WHILE;
            placeholder_pos = code_len;
            code[code_len++] = (word)0; /* placeholder */
            /* if label exists and defined, fill immediately; otherwise record a patch */
            LabelEntry *le = label_table_find(&labels, tokens[1]);
            if (le && le->defined) {
                code[placeholder_pos] = (word)le->pos;
            } else {
                if (whilepatch_add(&wpatches, tokens[1], placeholder_pos, err_msg) < 0) { free(kwlow); lexer_free_tokens(tokens); goto fail; }
            }
        } else if (strcasecmp(kwlow, "label") == 0) {
            if (ntok != 2) { set_error_msg(err_msg, "line %zu: label expects: label <name>", lineno); free(kwlow); lexer_free_tokens(tokens); goto fail; }
            if (label_table_add(&labels, tokens[1], code_len, 1, err_msg) < 0) { free(kwlow); lexer_free_tokens(tokens); goto fail; }
            /* backpatch any while patches for this label */
            for (size_t pi = 0; pi < wpatches.count; ++pi) {
                if (strcmp(wpatches.arr[pi].label, tokens[1]) == 0) {
                    size_t immpos = wpatches.arr[pi].imm_pos;
                    code[immpos] = (word)code_len;
                    free(wpatches.arr[pi].label);
                    wpatches.arr[pi] = wpatches.arr[wpatches.count - 1];
                    wpatches.count--;
                    pi--;
                }
            }
        } else if (strcasecmp(kwlow, "halt") == 0) {
            EMIT0(OP_HALT);
        } else if (strcasecmp(kwlow, "or") == 0 || strcasecmp(kwlow, "orassign") == 0) { EMIT0(OP_ORASSign);
        } else if (strcasecmp(kwlow, "and") == 0 || strcasecmp(kwlow, "andassign") == 0) { EMIT0(OP_ANDASSign);
        } else if (strcasecmp(kwlow, "not") == 0) { EMIT0(OP_NOT);
        } else if (strcasecmp(kwlow, "bitand") == 0) { EMIT0(OP_BITAND);
        } else if (strcasecmp(kwlow, "bitor") == 0) { EMIT0(OP_BITOR);
        } else if (strcasecmp(kwlow, "bitxor") == 0) { EMIT0(OP_BITXOR);
        } else if (strcasecmp(kwlow, "lsh") == 0) { EMIT0(OP_LSH);
        } else if (strcasecmp(kwlow, "lrsh") == 0) { EMIT0(OP_LRSH);
        } else if (strcasecmp(kwlow, "arsh") == 0) { EMIT0(OP_ARSH);
        } else if (strcasecmp(kwlow, "gez") == 0) { EMIT0(OP_GEZ);
        } else {
            set_error_msg(err_msg, "line %zu: unknown keyword '%s'", lineno, tokens[0]);
            free(kwlow);
            lexer_free_tokens(tokens);
            goto fail;
        }

        free(kwlow);
        lexer_free_tokens(tokens);

        line = strtok(NULL, "\n");
        continue;

    fail:
        /* cleanup state for failure */
        free(buf);
        label_table_free(&labels);
        whilepatch_free(&wpatches);
        func_table_free(&funcs);
        free(code);
        return -1;
    }

    /* finished reading lines */
    free(buf);

    /* backpatch remaining while patches */
    for (size_t i = 0; i < wpatches.count; ++i) {
        WhilePatch *wp = &wpatches.arr[i];
        LabelEntry *le = label_table_find(&labels, wp->label);
        if (!le || !le->defined) {
            set_error_msg(err_msg, "undefined label referenced by while: '%s'", wp->label);
            free_labeltable_and_patches(&labels, &wpatches);
            func_table_free(&funcs);
            free(code);
            return -1;
        }
        code[wp->imm_pos] = (word)le->pos;
    }

    /* ensure all functions that were referenced have been defined */
    for (size_t i = 0; i < funcs.count; ++i) {
        if (!funcs.arr[i].defined) {
            set_error_msg(err_msg, "undefined function referenced: '%s'", funcs.arr[i].name);
            label_table_free(&labels);
            whilepatch_free(&wpatches);
            func_table_free(&funcs);
            free(code);
            return -1;
        }
    }

    /* Success: populate out_vm */
    out_vm->code = code;
    out_vm->code_len = code_len;
    out_vm->ip = 0;
    out_vm->sp = 0;
    out_vm->tp = 0;
    out_vm->tp_sp = 0;
    out_vm->call_sp = 0;
    out_vm->fp = 0;
    out_vm->functions_count = 0;
    out_vm->user_data = NULL;
    /* Note: vm->functions table will be filled at runtime when OP_FUNCTION hooks run (backends). */

    /* cleanup tables (retain code) */
    label_table_free(&labels);
    whilepatch_free(&wpatches);
    func_table_free(&funcs);

    return 0;
}

int parse_rr_file_to_vm(const char *path, VM *out_vm, char **err_msg) {
    if (!path || !out_vm) {
        set_error_msg(err_msg, "invalid arguments");
        return -1;
    }

    /* Read file into memory (or stdin) */
    FILE *f = NULL;
    int from_stdin = (strcmp(path, "-") == 0);
    if (from_stdin) f = stdin;
    else {
        f = fopen(path, "r");
        if (!f) {
            set_error_msg(err_msg, "cannot open '%s': %s", path, strerror(errno));
            return -1;
        }
    }

    /* Read whole file into buffer */
    size_t cap = 0, len = 0;
    char *buf = NULL;
    char readbuf[4096];
    while (1) {
        size_t r = fread(readbuf, 1, sizeof(readbuf), f);
        if (r > 0) {
            if (len + r + 1 > cap) {
                size_t nc = cap ? cap * 2 : 4096;
                while (nc < len + r + 1) nc *= 2;
                char *n = (char*)realloc(buf, nc);
                if (!n) {
                    free(buf);
                    if (!from_stdin) fclose(f);
                    set_error_msg(err_msg, "out of memory");
                    return -1;
                }
                buf = n; cap = nc;
            }
            memcpy(buf + len, readbuf, r);
            len += r;
        }
        if (r < sizeof(readbuf)) break;
    }
    if (!from_stdin) fclose(f);
    if (!buf) {
        /* empty file -> treat as empty string */
        buf = xstrdup("");
        if (!buf) { set_error_msg(err_msg, "out of memory"); return -1; }
    }
    buf[len] = '\0';

    int ret = parse_rr_string_to_vm(buf, out_vm, err_msg);
    free(buf);
    return ret;
}

void parser_free_vm_code(VM *vm) {
    if (!vm) return;
    if (vm->code) {
        free((void*)vm->code);
        vm->code = NULL;
    }
    vm->code_len = 0;
}

/* Portable xstrdup implementation appended here to ensure the symbol is
 * available for earlier uses in this translation unit. Some platforms
 * provide strdup in string.h but to avoid implicit-declaration issues we
 * provide a small local implementation. */
static char *xstrdup(const char *s) {
    if (!s) return NULL;
    size_t n = strlen(s);
    char *r = (char*)malloc(n + 1);
    if (!r) return NULL;
    memcpy(r, s, n);
    r[n] = '\0';
    return r;
}