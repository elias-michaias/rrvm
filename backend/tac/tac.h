#ifndef TAC_BACKEND_H
#define TAC_BACKEND_H

#include "../../vm/vm.h"
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>

// --- TAC IR ---
typedef enum {
    TAC_CONST,
    TAC_ADD,
    TAC_SUB,
    TAC_MUL,
    TAC_DIV,
    TAC_MOVE,
    TAC_LOAD,
    TAC_STORE,
    TAC_PRINT,

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
} tac_instr;

typedef struct {
    tac_instr *code;
    size_t count;
    size_t cap;
} tac_prog;

// --- TAC backend state ---

typedef struct { OpCode type; int start_label; int else_label; int end_label; } tac_block_entry;

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
    fprintf(stderr, "[tac_fix_vm_map_after_insert] before: idx=%zu, count=%zu\n", idx, s->prog.count);
    for (size_t i = 0; i < s->vm_code_len; ++i) {
        if (s->vm_ip_to_tac_index[i] >= 0) fprintf(stderr, " vm_ip[%zu] -> %d\n", i, s->vm_ip_to_tac_index[i]);
    }

    for (size_t i = 0; i < s->vm_code_len; ++i) {
        if (s->vm_ip_to_tac_index[i] >= 0 && (size_t)s->vm_ip_to_tac_index[i] >= idx) {
            s->vm_ip_to_tac_index[i]++;
        }
    }

    /* diagnostic: print after fix */
    fprintf(stderr, "[tac_fix_vm_map_after_insert] after:\n");
    for (size_t i = 0; i < s->vm_code_len; ++i) {
        if (s->vm_ip_to_tac_index[i] >= 0) fprintf(stderr, " vm_ip[%zu] -> %d\n", i, s->vm_ip_to_tac_index[i]);
    }
}

static inline void tac_push(VM *vm, word imm) {
    tac_backend_state *s = tac_state(vm);
    /* compute opcode ip: PUSH consumes opcode+imm -> vm->ip - 2 */
    size_t opcode_ip = vm->ip >= 2 ? vm->ip - 2 : 0;
    tac_record_vm_ip(s, opcode_ip, (int)s->prog.count);

    int tmp = s->next_temp++;
    tac_emit(&s->prog, (tac_instr){.op=TAC_CONST, .dst=tmp, .imm=imm});
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
    tac_emit(&s->prog, (tac_instr){.op=op, .dst=dst, .lhs=lhs, .rhs=rhs});
    s->stack[s->sp++] = dst;
}

static void tac_add(VM *vm) { tac_binary(vm, TAC_ADD); }
static void tac_sub(VM *vm) { tac_binary(vm, TAC_SUB); }
static void tac_mul(VM *vm) { tac_binary(vm, TAC_MUL); }
static void tac_div(VM *vm) { tac_binary(vm, TAC_DIV); }

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
    tac_emit(&s->prog, (tac_instr){.op=TAC_STORE, .lhs=src});
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
    fprintf(stderr, "[tac_insert_label_at_idx] inserting label L%d at tac idx %zu (prog.count=%zu)\n", label, idx, s->prog.count);
    tac_insert_at(&s->prog, idx, (tac_instr){.op=TAC_LABEL, .imm=(word)label});
    /* diagnostics: show a few entries around idx */
    size_t start = idx > 3 ? idx - 3 : 0;
    size_t end = (idx + 3) < s->prog.count ? idx + 3 : s->prog.count - 1;
    for (size_t i = start; i <= end; ++i) {
        tac_instr *it = &s->prog.code[i];
        if (it->op == TAC_LABEL) fprintf(stderr, "  prog[%zu] = L%d\n", i, (int)it->imm);
        else if (it->op == TAC_JZ) fprintf(stderr, "  prog[%zu] = JZ t%d -> L%d\n", i, it->lhs, (int)it->imm);
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
    s->block_stack[s->block_sp++] = (tac_block_entry){ .type = OP_FUNCTION, .start_label = lbl, .else_label = 0, .end_label = 0 };
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
    s->block_stack[s->block_sp++] = (tac_block_entry){ .type = OP_IF, .start_label = 0, .else_label = else_label, .end_label = end_label };
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
            fprintf(stderr, "[tac_while] vm_ip %zu had no mapping (map_idx=%d)\n", cond_vm_ip, map_idx);
        }
    } else {
        fprintf(stderr, "[tac_while] no vm map or vm_ip out of range: vm_code_len=%zu cond_vm_ip=%zu\n", s->vm_code_len, cond_vm_ip);
    }
    if (cond_label < 0) {
        /* fallback: create a fresh cond label (will be placed later), but best-effort mapping failed */
        cond_label = tac_new_label(s);
        fprintf(stderr, "[tac_while] fallback cond_label L%d\n", cond_label);
    }

    int end_label = tac_new_label(s);
    /* if condition is false, jump to end; otherwise fall through into the loop body */
    tac_emit_jz(s, cond, end_label);
    /* emit body start label */
    int body_label = tac_new_label(s);
    tac_emit_label(s, body_label);
    s->block_stack[s->block_sp++] = (tac_block_entry){ .type = OP_WHILE, .start_label = cond_label, .else_label = 0, .end_label = end_label };
}

static void tac_endblock(VM *vm) {
    tac_backend_state *s = tac_state(vm);
    size_t opcode_ip = vm->ip > 0 ? vm->ip - 1 : 0;
    tac_record_vm_ip(s, opcode_ip, (int)s->prog.count);

    assert(s->block_sp > 0 && "ENDBLOCK without block");
    tac_block_entry b = s->block_stack[--s->block_sp];
    if (b.type == OP_WHILE) {
        /* at end of while, jump back to start (stored cond_label) and emit end label */
        tac_emit_jmp(s, b.start_label);
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
    .op_move = tac_move,
    .op_load = tac_load,
    .op_store= tac_store,
    .op_print= tac_print,

    /* control/call hooks in declaration order from vm/vm.h */
    .op_function = tac_function,
    .op_call     = tac_call,
    .op_return   = tac_return,
    .op_while    = tac_while,
    .op_if       = tac_if,
    .op_else     = tac_else,
    .op_endblock = tac_endblock,
};

// --- Dump TAC ---
static inline void tac_dump(const tac_prog *t) {
    for (size_t i = 0; i < t->count; i++) {
        const tac_instr *instr = &t->code[i];
        switch(instr->op) {
            case TAC_CONST: printf("t%d = %" WORD_FMT "\n", instr->dst, instr->imm); break;
            case TAC_ADD:   printf("t%d = t%d + t%d\n", instr->dst, instr->lhs, instr->rhs); break;
            case TAC_SUB:   printf("t%d = t%d - t%d\n", instr->dst, instr->lhs, instr->rhs); break;
            case TAC_MUL:   printf("t%d = t%d * t%d\n", instr->dst, instr->lhs, instr->rhs); break;
            case TAC_DIV:   printf("t%d = t%d / t%d\n", instr->dst, instr->lhs, instr->rhs); break;
            case TAC_MOVE:  printf("MOVE %" WORD_FMT "\n", instr->imm); break;
            case TAC_LOAD:  printf("t%d = LOAD\n", instr->dst); break;
            case TAC_STORE: printf("STORE t%d\n", instr->lhs); break;
            case TAC_PRINT: printf("PRINT t%d\n", instr->lhs); break;
            case TAC_LABEL: printf("L%d:\n", (int)instr->imm); break;
            case TAC_JMP:   printf("JMP L%d\n", (int)instr->imm); break;
            case TAC_JZ:    printf("JZ t%d -> L%d\n", instr->lhs, (int)instr->imm); break;
            case TAC_CALL:
                if (instr->dst >= 0) {
                    /* call with a destination temp */
                    printf("t%d = CALL %d\n", instr->dst, (int)instr->imm);
                } else {
                    /* call without destination */
                    printf("CALL %d\n", (int)instr->imm);
                }
                break;
            case TAC_RET:   printf("RET\n"); break;
        }
    }
}

// --- Access TAC program ---
static inline tac_prog *tac_get_prog(VM *vm) {
    return &((tac_backend_state*)vm->user_data)->prog;
}

#endif // TAC_BACKEND_H
