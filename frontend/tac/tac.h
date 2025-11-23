#ifndef TAC_BACKEND_H
#define TAC_BACKEND_H

#include "../vm/vm.h"
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>

/* enable debug diagnostics (set to 1 to enable verbose prints) */
#ifndef TAC_DEBUG
#define TAC_DEBUG 0
#endif

static void create_dir(const char *path) {
    char tmp[256];
    strncpy(tmp, path, sizeof(tmp)-1);
    tmp[sizeof(tmp)-1] = '\0';
    size_t len = strlen(tmp);
    if (len && tmp[len-1] == '/') tmp[len-1] = '\0';
    for (char *p = tmp + 1; *p; ++p) {
        if (*p == '/') {
            *p = '\0';
            mkdir(tmp, 0755);
            *p = '/';
        }
    }
    mkdir(tmp, 0755);
}

// --- TAC IR ---
typedef enum {
    TAC_CONST,
    TAC_ADD,
    TAC_SUB,
    TAC_MUL,
    TAC_DIV,
    TAC_REM,

    /* bitwise / logical / shifts */
    TAC_BITAND,
    TAC_BITOR,
    TAC_BITXOR,
    TAC_LSH,
    TAC_LRSH,
    TAC_ARSH,

    /* logical binary ops (produce 0/1) */
    TAC_OR,
    TAC_AND,
    TAC_NOT, /* unary: dst = !lhs */
    TAC_GEZ, /* unary: dst = lhs >= 0 */

    TAC_MOVE,
    TAC_LOAD,
    TAC_STORE,
    TAC_PRINT,
    TAC_PRINTCHAR,

    /* pointer / reference operations */
    TAC_DEREF, /* lhs = pointer temp, dst = result temp (load from tape/slot) */
    TAC_REFER, /* lhs = value temp, dst = pointer temp (create a reference) */
    TAC_WHERE, /* lhs = pointer temp, dst = result temp (get address/index) */
    TAC_OFFSET,/* lhs = pointer temp, rhs = offset temp, dst = result pointer temp */
    TAC_INDEX, /* lhs = pointer temp, rhs = index/temp, dst = result temp (load from indexed slot) */
    TAC_SET,   /* lhs = pointer/temp (target), rhs = value temp (source) -> store */

    /* control-flow / labels / calls */
    TAC_LABEL, /* imm = label id */
    TAC_JMP,   /* imm = target label */
    TAC_JZ,    /* lhs = cond temp, imm = target label */
    TAC_CALL,  /* imm = function index or label */
    TAC_RET,
} TacOp;

typedef struct {
    TacOp op;
    int dst;
    int lhs;
    int rhs;
    word imm;
    /* Optional type tag for the destination temp produced by this instruction.
       Stored as an integer corresponding to TypeTag. If no destination or
       unknown, this will be TYPE_UNKNOWN (0). */
    int dst_type;
} tac_instr;

typedef struct {
    tac_instr *code;
    size_t count;
    size_t cap;
} tac_prog;

// --- TAC backend state ---

typedef struct { OpCode type; int start_label; int else_label; int end_label; /* VM ip (size_t) for the condition start; (size_t)-1 if not set */ size_t cond_vm_ip; } tac_block_entry;

typedef struct {
    tac_prog prog;
    int stack[STACK_SIZE];
    int sp;
    int next_temp;
    /* keep a virtual tape pointer for MOVE semantics at TAC construction time (optional) */
    size_t tp;

    /* label generation and block stack for structured control flow */
    int label_counter;
    tac_block_entry block_stack[256];
    int block_sp;

    /* mapping from VM function index -> TAC label id to avoid collisions */
    int func_label[256];

    /* mapping from VM opcode ip -> tac instruction index. allocated at setup to vm->code_len */
    int *vm_ip_to_tac_index;
    /* mapping from VM opcode ip -> tac label id (if we insert a label to mark vm-ip) */
    int *vm_ip_to_tac_label;
    size_t vm_code_len;

    /* per-temp type information (TypeTag stored as int). Dynamically grown as temps are allocated. */
    int *temp_types;
    int temp_cap;
} tac_backend_state;

// --- Helpers ---
static inline void tac_init(tac_prog *t) {
    t->code = NULL;
    t->count = 0;
    t->cap = 0;
}

static inline void tac_free(tac_prog *t) {
    free(t->code);
    t->code = NULL;
    t->count = t->cap = 0;
}

static inline void tac_emit(tac_prog *t, tac_instr instr) {
    if (t->count == t->cap) {
        t->cap = t->cap ? t->cap * 2 : 8;
        t->code = (tac_instr*)realloc(t->code, t->cap * sizeof(tac_instr));
    }
    t->code[t->count++] = instr;
}

/* insert a tac instruction at index 'idx' shifting the rest forward */
static void tac_insert_at(tac_prog *t, size_t idx, tac_instr instr) {
    if (idx > t->count) idx = t->count;
    if (t->count + 1 > t->cap) {
        t->cap = t->cap ? t->cap * 2 : 8;
        t->code = (tac_instr*)realloc(t->code, t->cap * sizeof(tac_instr));
    }
    /* shift elements right */
    for (size_t i = t->count; i > idx; --i) {
        t->code[i] = t->code[i - 1];
    }
    t->code[idx] = instr;
    t->count++;
}

// --- Backend functions ---
static void tac_setup(VM *vm) {
    tac_backend_state *s = (tac_backend_state*)malloc(sizeof(tac_backend_state));
    s->sp = 0;
    s->next_temp = 0;
    s->tp = 0;
    s->label_counter = 1; /* start label ids at 1 */
    s->block_sp = 0;
    s->vm_code_len = vm->code_len;
    s->temp_types = NULL;
    s->temp_cap = 0;
    /* init func_label mapping to -1 (unused) */
    for (size_t i = 0; i < sizeof(s->func_label)/sizeof(s->func_label[0]); ++i) s->func_label[i] = -1;
    /* allocate vm_ip -> tac index map and vm_ip -> tac label map and initialize to -1 */
    if (s->vm_code_len) {
        s->vm_ip_to_tac_index = (int*)malloc(sizeof(int) * s->vm_code_len);
        s->vm_ip_to_tac_label = (int*)malloc(sizeof(int) * s->vm_code_len);
        for (size_t i = 0; i < s->vm_code_len; ++i) { s->vm_ip_to_tac_index[i] = -1; s->vm_ip_to_tac_label[i] = -1; }
    } else {
        s->vm_ip_to_tac_index = NULL;
        s->vm_ip_to_tac_label = NULL;
    }
    tac_init(&s->prog);
    vm->user_data = s;
}

static void tac_finalize(VM *vm, word imm) {
    tac_backend_state *s = (tac_backend_state*)vm->user_data;
    tac_free(&s->prog);
    free(s->vm_ip_to_tac_index);
    free(s->vm_ip_to_tac_label);
    free(s->temp_types);
    free(s);
    vm->user_data = NULL;
}

static tac_backend_state *tac_state(VM *vm) {
    return (tac_backend_state*)vm->user_data;
}

/* record the mapping from vm opcode ip -> tac instr index */
static inline void tac_record_vm_ip(tac_backend_state *s, size_t vm_ip, int tac_index) {
    if (!s->vm_ip_to_tac_index) return;
    if (vm_ip < s->vm_code_len) s->vm_ip_to_tac_index[vm_ip] = tac_index;
}

/* after inserting at idx, bump all vm_ip mappings that pointed at >= idx */
static void tac_fix_vm_map_after_insert(tac_backend_state *s, size_t idx) {
    if (!s->vm_ip_to_tac_index) return;
    /* diagnostic: print a small snapshot before fix */
    if (TAC_DEBUG) {
        fprintf(stderr, "[tac_fix_vm_map_after_insert] before: idx=%zu, count=%zu\n", idx, s->prog.count);
        for (size_t i = 0; i < s->vm_code_len; ++i) {
            if (s->vm_ip_to_tac_index[i] >= 0) fprintf(stderr, " vm_ip[%zu] -> %d\n", i, s->vm_ip_to_tac_index[i]);
        }
    }

    /* when we bump indices, increment any vm_ip mappings that pointed at or after the insertion index.
       vm_ip_to_tac_label is keyed by vm_ip (not by tac index), so label entries remain attached to the
       originating vm_ip and do not require shifting here; avoid clearing them. */
    for (size_t i = 0; i < s->vm_code_len; ++i) {
        if (s->vm_ip_to_tac_index[i] >= 0 && (size_t)s->vm_ip_to_tac_index[i] >= idx) {
            s->vm_ip_to_tac_index[i]++;
        }
    }

    /* diagnostic: print after fix */
    if (TAC_DEBUG) {
        fprintf(stderr, "[tac_fix_vm_map_after_insert] after:\n");
        for (size_t i = 0; i < s->vm_code_len; ++i) {
            if (s->vm_ip_to_tac_index[i] >= 0) fprintf(stderr, " vm_ip[%zu] -> %d\n", i, s->vm_ip_to_tac_index[i]);
            if (s->vm_ip_to_tac_label && s->vm_ip_to_tac_label[i] >= 0) fprintf(stderr, " vm_ip[%zu] -> L%d (label)\n", i, s->vm_ip_to_tac_label[i]);
        }
    }
}

static inline void tac_ensure_temp_capacity(tac_backend_state *s, int needed) {
    if (needed < s->temp_cap) return;
    int newcap = s->temp_cap ? s->temp_cap * 2 : 16;
    while (newcap <= needed) newcap *= 2;
    s->temp_types = (int*)realloc(s->temp_types, sizeof(int) * newcap);
    /* initialize new slots to TYPE_UNKNOWN */
    for (int i = s->temp_cap; i < newcap; ++i) s->temp_types[i] = TYPE_UNKNOWN;
    s->temp_cap = newcap;
}

static inline void tac_push(VM *vm, int type, word imm) {
    tac_backend_state *s = tac_state(vm);
    /* compute opcode ip: PUSH consumes opcode + type + imm -> vm->ip - 3 */
    size_t opcode_ip = vm->ip >= 3 ? vm->ip - 3 : 0;
    tac_record_vm_ip(s, opcode_ip, (int)s->prog.count);

    int tmp = s->next_temp++;
    /* ensure temp_types can hold this temp id */
    tac_ensure_temp_capacity(s, tmp);
    s->temp_types[tmp] = type;
    tac_emit(&s->prog, (tac_instr){.op=TAC_CONST, .dst=tmp, .imm=imm, .dst_type=type});
    s->stack[s->sp++] = tmp;
}

static inline void tac_binary(VM *vm, TacOp op) {
    tac_backend_state *s = tac_state(vm);
    /* binary ops consume only opcode -> vm->ip - 1 */
    size_t opcode_ip = vm->ip > 0 ? vm->ip - 1 : 0;
    tac_record_vm_ip(s, opcode_ip, (int)s->prog.count);

    assert(s->sp >= 2);
    int rhs = s->stack[--s->sp];
    int lhs = s->stack[--s->sp];
    int dst = s->next_temp++;
    /* infer dst type from lhs (require toolchain to produce well-typed input) */
    tac_ensure_temp_capacity(s, dst);
    int inferred_type = TYPE_UNKNOWN;
    if (lhs >= 0 && lhs < s->temp_cap) inferred_type = s->temp_types[lhs];
    s->temp_types[dst] = inferred_type;
    tac_emit(&s->prog, (tac_instr){.op=op, .dst=dst, .lhs=lhs, .rhs=rhs, .dst_type=inferred_type});
    s->stack[s->sp++] = dst;
}

static void tac_add(VM *vm) { tac_binary(vm, TAC_ADD); }
static void tac_sub(VM *vm) { tac_binary(vm, TAC_SUB); }
static void tac_mul(VM *vm) { tac_binary(vm, TAC_MUL); }
static void tac_div(VM *vm) { tac_binary(vm, TAC_DIV); }
static void tac_rem(VM *vm) { tac_binary(vm, TAC_REM); }

/* logical OR/AND (binary producing 0/1) */
static void tac_orassign(VM *vm) { tac_binary(vm, TAC_OR); }
static void tac_andassign(VM *vm) { tac_binary(vm, TAC_AND); }

/* unary and bitwise/shift lowerings */
static void tac_not(VM *vm) {
    tac_backend_state *s = tac_state(vm);
    size_t opcode_ip = vm->ip > 0 ? vm->ip - 1 : 0;
    tac_record_vm_ip(s, opcode_ip, (int)s->prog.count);

    assert(s->sp >= 1 && "tac_not: missing operand temp");
    int lhs = s->stack[--s->sp];
    int dst = s->next_temp++;
    tac_emit(&s->prog, (tac_instr){.op=TAC_NOT, .dst=dst, .lhs=lhs});
    s->stack[s->sp++] = dst;
}

static void tac_gez(VM *vm) {
    tac_backend_state *s = tac_state(vm);
    size_t opcode_ip = vm->ip > 0 ? vm->ip - 1 : 0;
    tac_record_vm_ip(s, opcode_ip, (int)s->prog.count);

    assert(s->sp >= 1 && "tac_gez: missing operand temp");
    int lhs = s->stack[--s->sp];
    int dst = s->next_temp++;
    tac_emit(&s->prog, (tac_instr){.op=TAC_GEZ, .dst=dst, .lhs=lhs});
    s->stack[s->sp++] = dst;
}

static void tac_bitand(VM *vm) { tac_binary(vm, TAC_BITAND); }
static void tac_bitor(VM *vm) { tac_binary(vm, TAC_BITOR); }
static void tac_bitxor(VM *vm) { tac_binary(vm, TAC_BITXOR); }
static void tac_lsh(VM *vm) { tac_binary(vm, TAC_LSH); }
static void tac_lrsh(VM *vm) { tac_binary(vm, TAC_LRSH); }
static void tac_arsh(VM *vm) { tac_binary(vm, TAC_ARSH); }

static void tac_move(VM *vm, word imm) {
    tac_backend_state *s = tac_state(vm);
    /* MOVE consumes opcode+imm -> vm->ip - 2 */
    size_t opcode_ip = vm->ip >= 2 ? vm->ip - 2 : 0;
    tac_record_vm_ip(s, opcode_ip, (int)s->prog.count);

    tac_emit(&s->prog, (tac_instr){.op=TAC_MOVE, .imm=imm});
    if (imm < 0) {
        size_t step = (size_t)(-imm);
        assert(s->tp >= step && "TAC virtual tape pointer underflow");
        s->tp -= step;
    } else {
        s->tp += (size_t)imm;
        assert(s->tp < TAPE_SIZE && "TAC virtual tape pointer overflow");
    }
}

static void tac_store(VM *vm) {
    tac_backend_state *s = tac_state(vm);
    size_t opcode_ip = vm->ip > 0 ? vm->ip - 1 : 0;
    tac_record_vm_ip(s, opcode_ip, (int)s->prog.count);

    assert(s->sp >= 1);
    int src = s->stack[--s->sp];
    tac_emit(&s->prog, (tac_instr){TAC_STORE, -1, src, 0, 0});
}

static void tac_load(VM *vm) {
    tac_backend_state *s = tac_state(vm);
    size_t opcode_ip = vm->ip > 0 ? vm->ip - 1 : 0;
    tac_record_vm_ip(s, opcode_ip, (int)s->prog.count);

    int dst = s->next_temp++;
    tac_emit(&s->prog, (tac_instr){.op=TAC_LOAD, .dst=dst});
    s->stack[s->sp++] = dst;
}

static void tac_print(VM *vm) {
    tac_backend_state *s = tac_state(vm);
    size_t opcode_ip = vm->ip > 0 ? vm->ip - 1 : 0;
    tac_record_vm_ip(s, opcode_ip, (int)s->prog.count);

    assert(s->sp >= 1);
    int val = s->stack[--s->sp];
    tac_emit(&s->prog, (tac_instr){.op=TAC_PRINT, .lhs=val});
}

static void tac_print_char(VM *vm) {
    tac_backend_state *s = tac_state(vm);
    size_t opcode_ip = vm->ip > 0 ? vm->ip - 1 : 0;
    tac_record_vm_ip(s, opcode_ip, (int)s->prog.count);

    assert(s->sp >= 1);
    int val = s->stack[--s->sp];
    tac_emit(&s->prog, (tac_instr){.op=TAC_PRINTCHAR, .lhs=val});
}

// --- Pointer-op lowering helpers (VM -> TAC) ---
/*
 * Lowering strategy for pointer ops to TAC (explicit SSA temps) — strict explicit-only model.
 *
 * All pointer/value-producing ops must consume and produce explicit temps on the TAC virtual
 * stack. Implicit 'tp' fallbacks have been removed. Missing temps are treated as programming
 * errors and assert during TAC emission to catch upstream lowering bugs early.
 */
static void tac_deref(VM *vm) {
    tac_backend_state *s = tac_state(vm);
    size_t opcode_ip = vm->ip > 0 ? vm->ip - 1 : 0;
    tac_record_vm_ip(s, opcode_ip, (int)s->prog.count);

    /* explicit pointer path only: require a pointer temp on the virtual stack */
    assert(s->sp >= 1 && "tac_deref: missing pointer temp on virtual stack");
    int lhs = s->stack[--s->sp];
    int dst = s->next_temp++;
    tac_emit(&s->prog, (tac_instr){.op=TAC_DEREF, .dst=dst, .lhs=lhs});
    s->stack[s->sp++] = dst;
}

static void tac_refer(VM *vm) {
    tac_backend_state *s = tac_state(vm);
    size_t opcode_ip = vm->ip > 0 ? vm->ip - 1 : 0;
    tac_record_vm_ip(s, opcode_ip, (int)s->prog.count);

    /* explicit value -> pointer: require a value temp on the virtual stack */
    assert(s->sp >= 1 && "tac_refer: missing value temp on virtual stack");
    int lhs = s->stack[--s->sp];
    int dst = s->next_temp++;
    tac_emit(&s->prog, (tac_instr){.op=TAC_REFER, .dst=dst, .lhs=lhs});
    s->stack[s->sp++] = dst;
}

static void tac_where(VM *vm) {
    tac_backend_state *s = tac_state(vm);
    size_t opcode_ip = vm->ip > 0 ? vm->ip - 1 : 0;
    tac_record_vm_ip(s, opcode_ip, (int)s->prog.count);

    /* produce an explicit address/temp */
    int dst = s->next_temp++;
    tac_emit(&s->prog, (tac_instr){.op=TAC_WHERE, .dst=dst});
    s->stack[s->sp++] = dst;
}

static void tac_offset(VM *vm, word imm) {
    tac_backend_state *s = tac_state(vm);
    size_t opcode_ip = vm->ip >= 2 ? vm->ip - 2 : 0;
    tac_record_vm_ip(s, opcode_ip, (int)s->prog.count);

    /* explicit pointer path only: require a pointer temp on the virtual stack */
    assert(s->sp >= 1 && "tac_offset: missing pointer temp on virtual stack");
    int lhs = s->stack[--s->sp];
    int dst = s->next_temp++;
    tac_emit(&s->prog, (tac_instr){.op=TAC_OFFSET, .dst=dst, .lhs=lhs, .imm=imm});
    s->stack[s->sp++] = dst;
}

static void tac_index(VM *vm) {
    tac_backend_state *s = tac_state(vm);
    size_t opcode_ip = vm->ip > 0 ? vm->ip - 1 : 0;
    tac_record_vm_ip(s, opcode_ip, (int)s->prog.count);

    /* require pointer and index temps on the virtual stack */
    assert(s->sp >= 2 && "tac_index: missing pointer/index temps on virtual stack");
    int rhs = s->stack[--s->sp];
    int lhs = s->stack[--s->sp];
    int dst = s->next_temp++;
    tac_emit(&s->prog, (tac_instr){TAC_INDEX, dst, lhs, rhs, 0});
    s->stack[s->sp++] = dst;
}

static void tac_set(VM *vm, int type, word imm) {
    tac_backend_state *s = tac_state(vm);
    /* SET consumes opcode + type + imm -> vm->ip - 3 */
    size_t opcode_ip = vm->ip >= 3 ? vm->ip - 3 : 0;
    tac_record_vm_ip(s, opcode_ip, (int)s->prog.count);

    /* create a temp for the immediate value with proper type */
    int valtmp = s->next_temp++;
    tac_ensure_temp_capacity(s, valtmp);
    s->temp_types[valtmp] = type;
    tac_emit(&s->prog, (tac_instr){.op=TAC_CONST, .dst=valtmp, .imm=imm, .dst_type=type});

    /* Prefer using an explicit pointer temp from the virtual stack. If none is present,
       materialize the current pointer as a temp via WHERE and push it. We deliberately do
       not pop the pointer temp for SET so the pointer remains available for subsequent
       pointer ops (e.g. DEREF/REFER), matching VM semantics where the pointer itself is not
       consumed by a store. */
    int lhs;
    if (s->sp >= 1) {
        /* peek top-of-stack pointer temp without popping */
        lhs = s->stack[s->sp - 1];
    } else {
        lhs = s->next_temp++;
        tac_ensure_temp_capacity(s, lhs);
        s->temp_types[lhs] = TYPE_PTR;
        tac_emit(&s->prog, (tac_instr){.op=TAC_WHERE, .dst=lhs, .dst_type=TYPE_PTR});
        /* push the materialized pointer temp onto the virtual stack */
        s->stack[s->sp++] = lhs;
    }

    tac_emit(&s->prog, (tac_instr){.op=TAC_SET, .lhs=lhs, .rhs=valtmp});
}

// --- New control-flow emit helpers ---
static inline int tac_new_label(tac_backend_state *s) {
    return s->label_counter++;
}

static void tac_emit_label(tac_backend_state *s, int label) {
    tac_emit(&s->prog, (tac_instr){.op=TAC_LABEL, .imm=(word)label});
}

static void tac_emit_jmp(tac_backend_state *s, int label) {
    tac_emit(&s->prog, (tac_instr){.op=TAC_JMP, .imm=(word)label});
}

static void tac_emit_jz(tac_backend_state *s, int cond_temp, int label) {
    tac_emit(&s->prog, (tac_instr){.op=TAC_JZ, .lhs=cond_temp, .imm=(word)label});
}

static void tac_emit_call(tac_backend_state *s, int dst, int func_idx) {
    tac_emit(&s->prog, (tac_instr){.op=TAC_CALL, .dst=dst, .imm=(word)func_idx});
}

static void tac_emit_ret(tac_backend_state *s) {
    tac_emit(&s->prog, (tac_instr){.op=TAC_RET});
}

/* insert a TAC_LABEL at a specific tac instruction index and fix vm map (diagnostic) */
static void tac_insert_label_at_idx(tac_backend_state *s, size_t idx, int label) {
    if (TAC_DEBUG) fprintf(stderr, "[tac_insert_label_at_idx] inserting label L%d at tac idx %zu (prog.count=%zu)\n", label, idx, s->prog.count);

    /* before we shift vm->tac index map, record which vm_ip(s) pointed at idx so we can assign the label to them */
    if (s->vm_ip_to_tac_index && s->vm_ip_to_tac_label) {
        for (size_t i = 0; i < s->vm_code_len; ++i) {
            if (s->vm_ip_to_tac_index[i] == (int)idx) {
                s->vm_ip_to_tac_label[i] = label;
                if (TAC_DEBUG) fprintf(stderr, "  vm_ip[%zu] -> L%d\n", i, label);
            }
        }
    }

    tac_insert_at(&s->prog, idx, (tac_instr){.op=TAC_LABEL, .imm=(word)label});

    /* diagnostics: show a few entries around idx */
    if (TAC_DEBUG) {
        size_t start = idx > 3 ? idx - 3 : 0;
        size_t end = (idx + 3) < s->prog.count ? idx + 3 : s->prog.count - 1;
        for (size_t i = start; i <= end; ++i) {
            tac_instr *it = &s->prog.code[i];
            if (it->op == TAC_LABEL) fprintf(stderr, "  prog[%zu] = L%d\n", i, (int)it->imm);
            else if (it->op == TAC_JZ) fprintf(stderr, "  prog[%zu] = JZ t%d -> L%d\n", i, it->lhs, (int)it->imm);
        }
    }

    /* fix vm mapping */
    tac_fix_vm_map_after_insert(s, idx);
}

// --- Backend mappings from VM control opcodes to TAC ---
static void tac_function(VM *vm, word func_index) {
    tac_backend_state *s = tac_state(vm);
    /* FUNCTION consumes opcode+imm -> vm->ip - 2 */
    size_t opcode_ip = vm->ip >= 2 ? vm->ip - 2 : 0;
    tac_record_vm_ip(s, opcode_ip, (int)s->prog.count);

    int idx = (int)func_index;
    /* allocate a fresh label id for this function to avoid colliding with generated labels */
    int lbl = tac_new_label(s);
    s->func_label[idx] = lbl;
    tac_emit_label(s, lbl);
    /* push a FUNCTION block so ENDBLOCK can pop it safely */
    s->block_stack[s->block_sp++] = (tac_block_entry){ .type = OP_FUNCTION, .start_label = lbl, .else_label = 0, .end_label = 0, .cond_vm_ip = (size_t)-1 };
}

static void tac_call(VM *vm, word func_index) {
    tac_backend_state *s = tac_state(vm);
    /* CALL consumes opcode+imm -> vm->ip - 2 */
    size_t opcode_ip = vm->ip >= 2 ? vm->ip - 2 : 0;
    tac_record_vm_ip(s, opcode_ip, (int)s->prog.count);

    int idx = (int)func_index;
    int label = s->func_label[idx];
    if (label < 0) {
        /* forward call to a function not yet seen: allocate a label placeholder */
        label = tac_new_label(s);
        s->func_label[idx] = label;
    }
    /* allocate a destination temp for the call result (TAC-SSA style)
       and push it onto the virtual stack so subsequent ops can use it */
    int dst = s->next_temp++;
    tac_emit_call(s, dst, label);
    s->stack[s->sp++] = dst;
}

static void tac_return(VM *vm) {
    tac_backend_state *s = tac_state(vm);
    size_t opcode_ip = vm->ip > 0 ? vm->ip - 1 : 0;
    tac_record_vm_ip(s, opcode_ip, (int)s->prog.count);

    tac_emit_ret(s);
}

static void tac_if(VM *vm) {
    tac_backend_state *s = tac_state(vm);
    size_t opcode_ip = vm->ip > 0 ? vm->ip - 1 : 0;
    tac_record_vm_ip(s, opcode_ip, (int)s->prog.count);

    assert(s->sp >= 1);
    int cond = s->stack[--s->sp];
    int else_label = tac_new_label(s);
    int end_label = tac_new_label(s);
    /* emit conditional jump to else */
    tac_emit_jz(s, cond, else_label);
    /* push block info so ELSE/ENDBLOCK can emit labels */
    s->block_stack[s->block_sp++] = (tac_block_entry){ .type = OP_IF, .start_label = 0, .else_label = else_label, .end_label = end_label, .cond_vm_ip = (size_t)-1 };
}

static void tac_else(VM *vm) {
    tac_backend_state *s = tac_state(vm);
    size_t opcode_ip = vm->ip > 0 ? vm->ip - 1 : 0;
    tac_record_vm_ip(s, opcode_ip, (int)s->prog.count);

    assert(s->block_sp > 0);
    tac_block_entry b = s->block_stack[s->block_sp - 1];
    assert(b.type == OP_IF && "ELSE without matching IF");
    /* jump to end, then emit else label */
    tac_emit_jmp(s, b.end_label);
    tac_emit_label(s, b.else_label);
    /* mark block as ELSE so ENDBLOCK knows how to finish */
    s->block_stack[s->block_sp - 1].type = OP_ELSE;
}

static void tac_while(VM *vm, word cond_ip) {
    tac_backend_state *s = tac_state(vm);
    /* record mapping for the WHILE opcode itself (opcode+imm -> vm->ip - 2) */
    size_t opcode_ip = vm->ip >= 2 ? vm->ip - 2 : 0;
    tac_record_vm_ip(s, opcode_ip, (int)s->prog.count);

    /* expect the condition temp on the virtual stack */
    assert(s->sp >= 1);
    int cond = s->stack[--s->sp];

    /* try to map the cond_ip (VM ip) to a tac insertion index */
    int cond_label = -1;
    size_t cond_vm_ip = (size_t)cond_ip;
    if (s->vm_ip_to_tac_index && cond_vm_ip < s->vm_code_len) {
        int map_idx = s->vm_ip_to_tac_index[cond_vm_ip];
        if (map_idx >= 0) {
            /* insert a label at the tac instr index corresponding to the start of the condition */
            cond_label = tac_new_label(s);
            tac_insert_label_at_idx(s, (size_t)map_idx, cond_label);
        } else {
            if (TAC_DEBUG) fprintf(stderr, "[tac_while] vm_ip %zu had no mapping (map_idx=%d)\n", cond_vm_ip, map_idx);
        }
    } else {
        if (TAC_DEBUG) fprintf(stderr, "[tac_while] no vm map or vm_ip out of range: vm_code_len=%zu cond_vm_ip=%zu\n", s->vm_code_len, cond_vm_ip);
    }
    if (cond_label < 0) {
        /* fallback: create a fresh cond label (will be placed later), but best-effort mapping failed */
        cond_label = tac_new_label(s);
        if (TAC_DEBUG) fprintf(stderr, "[tac_while] fallback cond_label L%d\n", cond_label);
    }

    /* ensure we remember which TAC label corresponds to the condition VM ip so ENDBLOCK or other passes
       can reliably jump back to it. vm_ip_to_tac_label is keyed by vm_ip, so attach the cond_label there. */
    if (s->vm_ip_to_tac_label && cond_vm_ip < s->vm_code_len) {
        s->vm_ip_to_tac_label[cond_vm_ip] = cond_label;
        if (TAC_DEBUG) fprintf(stderr, "[tac_while] vm_ip[%zu] -> L%d\n", cond_vm_ip, cond_label);
    }

    int end_label = tac_new_label(s);
    /* if condition is false, jump to end; otherwise fall through into the loop body */
    tac_emit_jz(s, cond, end_label);
    /* emit body start label */
    int body_label = tac_new_label(s);
    tac_emit_label(s, body_label);
    s->block_stack[s->block_sp++] = (tac_block_entry){ .type = OP_WHILE, .start_label = cond_label, .else_label = 0, .end_label = end_label, .cond_vm_ip = cond_vm_ip };
}

static void tac_endblock(VM *vm) {
    tac_backend_state *s = tac_state(vm);
    size_t opcode_ip = vm->ip > 0 ? vm->ip - 1 : 0;
    tac_record_vm_ip(s, opcode_ip, (int)s->prog.count);

    assert(s->block_sp > 0 && "ENDBLOCK without block");
    tac_block_entry b = s->block_stack[--s->block_sp];
    if (b.type == OP_WHILE) {
        /* at end of while, jump back to start (prefer the stored cond label in the block entry).
           If the block entry doesn't have a valid start_label, fall back to scanning the
           vm_ip_to_tac_label map for any associated label (development-time fallback guarded by TAC_DEBUG).
           Finally assert that we found a label to avoid silent mis-compilation. */
        int target_label = b.start_label;
        if (target_label <= 0 && s->vm_ip_to_tac_label && s->vm_code_len) {
            /* scan vm_ip->label map for a candidate (best-effort fallback) */
            for (size_t i = 0; i < s->vm_code_len; ++i) {
                if (s->vm_ip_to_tac_label[i] > 0) {
                    target_label = s->vm_ip_to_tac_label[i];
                    if (TAC_DEBUG) fprintf(stderr, "[tac_endblock] fallback: using vm_ip[%zu] -> L%d as cond label\n", i, target_label);
                    break;
                }
            }
        }
        /* be strict in development to catch missing mappings */
        assert(target_label > 0 && "ENDBLOCK: missing condition label for WHILE");
        tac_emit_jmp(s, target_label);
        tac_emit_label(s, b.end_label);
    } else if (b.type == OP_IF || b.type == OP_ELSE) {
        /* just emit end label */
        tac_emit_label(s, b.end_label);
    } else if (b.type == OP_FUNCTION) {
        /* function block: nothing to emit; just popped */
        (void)0;
    } else {
        /* unknown block type */
        assert(0 && "Unknown block type in tac_endblock");
    }
}

// --- TAC backend struct ---
static const Backend __TAC = {
    .setup   = tac_setup,
    .finalize= tac_finalize,
    .op_push = tac_push,
    .op_add  = tac_add,
    .op_sub  = tac_sub,
    .op_mul  = tac_mul,
    .op_div  = tac_div,
    .op_rem  = tac_rem,
    .op_move = tac_move,
    .op_load = tac_load,
    .op_store= tac_store,
    .op_print= tac_print,
    .op_print_char = tac_print_char,

    /* pointer/reference hooks */
    .op_deref = tac_deref,
    .op_refer = tac_refer,
    .op_where = tac_where,
    .op_offset= tac_offset,
    .op_index = tac_index,
    .op_set   = tac_set,

    /* control/call hooks in declaration order from vm/vm.h */
    .op_function = tac_function,
    .op_call     = tac_call,
    .op_return   = tac_return,
    .op_while    = tac_while,
    .op_if       = tac_if,
    .op_else     = tac_else,
    .op_endblock = tac_endblock,

    /* new multi-element / bitwise / logical hooks */
    .op_orassign = tac_orassign,
    .op_andassign = tac_andassign,
    .op_not = tac_not,
    .op_bitand = tac_bitand,
    .op_bitor = tac_bitor,
    .op_bitxor = tac_bitxor,
    .op_lsh = tac_lsh,
    .op_lrsh = tac_lrsh,
    .op_arsh = tac_arsh,
    .op_gez = tac_gez,
};

// --- Dump TAC (predicate blocks) ---

/* helper: print a single TAC instruction as a Prolog goal (no trailing comma/period) */
static const char *type_tag_name(int t) {
    switch (t) {
        case TYPE_I8: return "i8";
        case TYPE_U8: return "u8";
        case TYPE_I16: return "i16";
        case TYPE_U16: return "u16";
        case TYPE_I32: return "i32";
        case TYPE_U32: return "u32";
        case TYPE_I64: return "i64";
        case TYPE_U64: return "u64";
        case TYPE_F32: return "f32";
        case TYPE_F64: return "f64";
        case TYPE_BOOL: return "bool";
        case TYPE_PTR: return "ptr";
        case TYPE_VOID: return "void";
        default: return "unknown";
    }
}

/* Print a single TAC instruction as a Prolog goal, including type annotation for
   destination temps when available (instr->dst_type). */
static void tac_print_goal(FILE *out, const tac_instr *instr) {
    switch (instr->op) {
        case TAC_CONST: {
            /* Print float constants as hex bit-patterns for clarity and append a
             * human-readable float value as a Prolog comment for convenience:
             *  - f32: 0xNNNNNNNN  (decimal: %f)
             *  - f64: 0xNNNNNNNNNNNNNNNN  (decimal: %f)
             * Fallback to the original numeric printing for non-float types.
             */
            if (instr->dst_type == TYPE_F32) {
                uint32_t bits = (uint32_t)(instr->imm & 0xFFFFFFFFu);
                /* reinterpret bits as float for a readable decimal comment */
                union { uint32_t u; float f; } u32;
                u32.u = bits;
                fprintf(out, "const(t%d, f32, 0x%08" PRIx32 " /* %f */)", instr->dst, bits, (double)u32.f);
            } else if (instr->dst_type == TYPE_F64) {
                uint64_t bits = (uint64_t)instr->imm;
                /* reinterpret bits as double for a readable decimal comment */
                union { uint64_t u; double d; } u64;
                u64.u = bits;
                fprintf(out, "const(t%d, f64, 0x%016" PRIx64 " /* %f */)", instr->dst, bits, u64.d);
            } else {
                fprintf(out, "const(t%d, %s, %" WORD_FMT ")", instr->dst, type_tag_name(instr->dst_type), instr->imm);
            }
            break;
        }
        case TAC_ADD:
            fprintf(out, "add(t%d, %s, t%d, t%d)", instr->dst, type_tag_name(instr->dst_type), instr->lhs, instr->rhs);
            break;
        case TAC_SUB:
            fprintf(out, "sub(t%d, %s, t%d, t%d)", instr->dst, type_tag_name(instr->dst_type), instr->lhs, instr->rhs);
            break;
        case TAC_MUL:
            fprintf(out, "mul(t%d, %s, t%d, t%d)", instr->dst, type_tag_name(instr->dst_type), instr->lhs, instr->rhs);
            break;
        case TAC_DIV:
            fprintf(out, "div(t%d, %s, t%d, t%d)", instr->dst, type_tag_name(instr->dst_type), instr->lhs, instr->rhs);
            break;
        case TAC_REM:
            fprintf(out, "rem(t%d, %s, t%d, t%d)", instr->dst, type_tag_name(instr->dst_type), instr->lhs, instr->rhs);
            break;
        case TAC_BITAND:
            fprintf(out, "bitand(t%d, %s, t%d, t%d)", instr->dst, type_tag_name(instr->dst_type), instr->lhs, instr->rhs);
            break;
        case TAC_BITOR:
            fprintf(out, "bitor(t%d, %s, t%d, t%d)", instr->dst, type_tag_name(instr->dst_type), instr->lhs, instr->rhs);
            break;
        case TAC_BITXOR:
            fprintf(out, "bitxor(t%d, %s, t%d, t%d)", instr->dst, type_tag_name(instr->dst_type), instr->lhs, instr->rhs);
            break;
        case TAC_LSH:
            fprintf(out, "lsh(t%d, %s, t%d, t%d)", instr->dst, type_tag_name(instr->dst_type), instr->lhs, instr->rhs);
            break;
        case TAC_LRSH:
            fprintf(out, "lrsh(t%d, %s, t%d, t%d)", instr->dst, type_tag_name(instr->dst_type), instr->lhs, instr->rhs);
            break;
        case TAC_ARSH:
            fprintf(out, "arsh(t%d, %s, t%d, t%d)", instr->dst, type_tag_name(instr->dst_type), instr->lhs, instr->rhs);
            break;
        case TAC_OR:
            fprintf(out, "or(t%d, bool, t%d, t%d)", instr->dst, instr->lhs, instr->rhs);
            break;
        case TAC_AND:
            fprintf(out, "and(t%d, bool, t%d, t%d)", instr->dst, instr->lhs, instr->rhs);
            break;
        case TAC_NOT:
            fprintf(out, "not(t%d, bool, t%d)", instr->dst, instr->lhs);
            break;
        case TAC_GEZ:
            fprintf(out, "gez(t%d, bool, t%d)", instr->dst, instr->lhs);
            break;
        case TAC_MOVE:
            fprintf(out, "move(%" WORD_FMT ")", instr->imm);
            break;
        case TAC_LOAD:
            fprintf(out, "load(t%d)", instr->dst);
            break;
        case TAC_STORE:
            fprintf(out, "store(t%d)", instr->lhs);
            break;
        case TAC_PRINT:
            fprintf(out, "print(t%d)", instr->lhs);
            break;
        case TAC_PRINTCHAR:
            fprintf(out, "printchar(t%d)", instr->lhs);
            break;
        case TAC_DEREF:
            fprintf(out, "deref(t%d, t%d)", instr->dst, instr->lhs);
            break;
        case TAC_REFER:
            fprintf(out, "refer(t%d, t%d)", instr->dst, instr->lhs);
            break;
        case TAC_WHERE:
            fprintf(out, "where(t%d)", instr->dst);
            break;
        case TAC_OFFSET:
            fprintf(out, "offset(t%d, t%d, %" WORD_FMT ")", instr->dst, instr->lhs, instr->imm);
            break;
        case TAC_INDEX:
            fprintf(out, "index(t%d, t%d, t%d)", instr->dst, instr->lhs, instr->rhs);
            break;
        case TAC_SET:
            fprintf(out, "set(t%d, t%d)", instr->lhs, instr->rhs);
            break;
        case TAC_JMP:
            fprintf(out, "jmp(l%d)", (int)instr->imm);
            break;
        case TAC_JZ:
            fprintf(out, "jz(t%d, l%d)", instr->lhs, (int)instr->imm);
            break;
        case TAC_CALL:
            if (instr->dst >= 0) fprintf(out, "call(l%d, t%d)", (int)instr->imm, instr->dst);
            else fprintf(out, "call(l%d)", (int)instr->imm);
            break;
        case TAC_RET:
            fprintf(out, "ret");
            break;
        case TAC_LABEL:
            /* labels handled by caller */
            fprintf(out, "true");
            break;
        default:
            fprintf(out, "unknown(%d)", instr->op);
            break;
    }
}

static void tac_dump_write(FILE *out, const tac_prog *t) {
    int curr_label = -1;
    size_t i = 0;
    while (i < t->count) {
        /* If we encounter a label, start a new predicate for it */
        if (t->code[i].op == TAC_LABEL) {
            int lbl = (int)t->code[i].imm;
            if (curr_label != -1) fprintf(out, "\n");
            curr_label = lbl;
            fprintf(out, "l%d :-\n", curr_label);
            i++;
            /* if label is terminal (next is label or end), emit true. */
            if (i >= t->count || t->code[i].op == TAC_LABEL) {
                fprintf(out, "  true.\n");
                continue;
            }
        } else {
            /* no label: ensure we are in an implicit top-level predicate l0 */
            if (curr_label != 0) {
                if (curr_label != -1) fprintf(out, "\n");
                curr_label = 0;
                fprintf(out, "l0 :-\n");
            }
        }

        /* Emit a sequence of goals until we hit a label or exhaust instructions.
           If we see a TAC_RET, close the current predicate immediately and
           start a new implicit l0 for any following non-label instructions. */
        /* print first goal */
        fprintf(out, "  ");
        tac_print_goal(out, &t->code[i]);
        /* if first goal is a RET, close and advance */
        if (t->code[i].op == TAC_RET) {
            fprintf(out, ".\n");
            i++;
            /* if next is non-label and exists, ensure implicit l0 will be emitted in next loop iteration */
            continue;
        }
        i++;
        while (i < t->count && t->code[i].op != TAC_LABEL) {
            fprintf(out, ",\n  ");
            tac_print_goal(out, &t->code[i]);
            if (t->code[i].op == TAC_RET) {
                fprintf(out, ".\n");
                i++;
                break;
            }
            i++;
        }
        /* if we exited because next is label or end and we did not already close with a RET, close the clause */
        if (i >= t->count || (i < t->count && t->code[i].op == TAC_LABEL)) {
            /* ensure we haven't just closed after a RET (which already printed a period) */
            /* look back to see if previous printed goal was a RET: if previous instr is RET, then we already closed */
            size_t prev_idx = i ? i - 1 : 0;
            if (!(i > 0 && t->code[prev_idx].op == TAC_RET)) {
                fprintf(out, ".\n");
            }
        }
    }
}

static void tac_dump_file(const tac_prog *t, const char *path) {
    /* create parent dir if needed */
    create_dir("opt/tmp/raw");

    /* Determine output filename from the provided `path` argument.
     *
     * Expected behavior:
     *  - If `path` is a source filename (e.g. "/some/dir/foo.rr" or "foo.rr"),
     *    use the basename without extension and write to "opt/tmp/raw/foo.pl".
     *  - If `path` is NULL or empty, fall back to "parsed.pl" -> "opt/tmp/raw/parsed.pl".
     *
     * Note: main.c should pass the original input filename as `path` when
     * calling tac_dump_file. This backend will not attempt to guess names
     * from other context.
     */
    char namebuf[256] = {0};

    if (path && path[0]) {
        const char *base = strrchr(path, '/');
        const char *b = base ? (base + 1) : path;
        /* copy basename and strip extension if present */
        strncpy(namebuf, b, sizeof(namebuf) - 1);
        char *dot = strrchr(namebuf, '.');
        if (dot) *dot = '\0';
        /* if stripping produced empty name, fall back to 'parsed' */
        if (namebuf[0] == '\0') strncpy(namebuf, "parsed", sizeof(namebuf) - 1);
    } else {
        strncpy(namebuf, "parsed", sizeof(namebuf) - 1);
    }

    char outpath[512];
    snprintf(outpath, sizeof(outpath), "opt/tmp/raw/%s.pl", namebuf);

    FILE *f = fopen(outpath, "w");
    if (!f) {
        perror("fopen");
        return;
    }
    tac_dump_write(f, t);
    fclose(f);
}

static inline void tac_dump(const tac_prog *t) {
    tac_dump_write(stdout, t);
}

// --- Access TAC program ---
static inline tac_prog *tac_get_prog(VM *vm) {
    return &((tac_backend_state*)vm->user_data)->prog;
}

#endif // TAC_BACKEND_H
