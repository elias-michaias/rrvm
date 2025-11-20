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
    TAC_CALL,  /* imm = function index */
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

// --- TAC backend functions ---
static void tac_setup(VM *vm) {
    tac_backend_state *s = (tac_backend_state*)malloc(sizeof(tac_backend_state));
    s->sp = 0;
    s->next_temp = 0;
    s->tp = 0;
    s->label_counter = 1; /* start label ids at 1 */
    s->block_sp = 0;
    tac_init(&s->prog);
    vm->user_data = s;
}

static void tac_finalize(VM *vm, word imm) {
    tac_backend_state *s = (tac_backend_state*)vm->user_data;
    tac_free(&s->prog);
    free(s);
    vm->user_data = NULL;
}

static tac_backend_state *tac_state(VM *vm) {
    return (tac_backend_state*)vm->user_data;
}

static void tac_push(VM *vm, word imm) {
    tac_backend_state *s = tac_state(vm);
    int tmp = s->next_temp++;
    tac_emit(&s->prog, (tac_instr){.op=TAC_CONST, .dst=tmp, .imm=imm});
    s->stack[s->sp++] = tmp;
}

static void tac_binary(VM *vm, TacOp op) {
    tac_backend_state *s = tac_state(vm);
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
    /* record a MOVE with immediate; no stack change */
    tac_emit(&s->prog, (tac_instr){.op=TAC_MOVE, .imm=imm});
    /* update virtual tp if needed */
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
    assert(s->sp >= 1);
    int src = s->stack[--s->sp];
    /* emit store, using lhs to hold the source temp */
    tac_emit(&s->prog, (tac_instr){.op=TAC_STORE, .lhs=src});
}

static void tac_load(VM *vm) {
    tac_backend_state *s = tac_state(vm);
    int dst = s->next_temp++;
    tac_emit(&s->prog, (tac_instr){.op=TAC_LOAD, .dst=dst});
    s->stack[s->sp++] = dst;
}

static void tac_print(VM *vm) {
    tac_backend_state *s = tac_state(vm);
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

static void tac_emit_call(tac_backend_state *s, int func_idx) {
    tac_emit(&s->prog, (tac_instr){.op=TAC_CALL, .imm=(word)func_idx});
}

static void tac_emit_ret(tac_backend_state *s) {
    tac_emit(&s->prog, (tac_instr){.op=TAC_RET});
}

// --- Backend mappings from VM control opcodes to TAC ---
static void tac_function(VM *vm, word func_index) {
    tac_backend_state *s = tac_state(vm);
    /* mark function entry with a label using the function index as label id */
    tac_emit_label(s, (int)func_index);
    /* push a FUNCTION block so ENDBLOCK can pop it safely */
    s->block_stack[s->block_sp++] = (tac_block_entry){ .type = OP_FUNCTION, .start_label = (int)func_index, .else_label = 0, .end_label = 0 };
}

static void tac_call(VM *vm, word func_index) {
    tac_backend_state *s = tac_state(vm);
    tac_emit_call(s, (int)func_index);
}

static void tac_return(VM *vm) {
    tac_backend_state *s = tac_state(vm);
    tac_emit_ret(s);
}

static void tac_if(VM *vm) {
    tac_backend_state *s = tac_state(vm);
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
    assert(s->block_sp > 0);
    tac_block_entry b = s->block_stack[s->block_sp - 1];
    assert(b.type == OP_IF && "ELSE without matching IF");
    /* jump to end, then emit else label */
    tac_emit_jmp(s, b.end_label);
    tac_emit_label(s, b.else_label);
    /* mark block as ELSE so ENDBLOCK knows how to finish */
    s->block_stack[s->block_sp - 1].type = OP_ELSE;
}

static void tac_while(VM *vm) {
    tac_backend_state *s = tac_state(vm);
    int start_label = tac_new_label(s);
    int end_label = tac_new_label(s);
    tac_emit_label(s, start_label);
    s->block_stack[s->block_sp++] = (tac_block_entry){ .type = OP_WHILE, .start_label = start_label, .else_label = 0, .end_label = end_label };
}

static void tac_endblock(VM *vm) {
    tac_backend_state *s = tac_state(vm);
    assert(s->block_sp > 0 && "ENDBLOCK without block");
    tac_block_entry b = s->block_stack[--s->block_sp];
    if (b.type == OP_WHILE) {
        /* at end of while, jump back to start and emit end label */
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

    .op_function = tac_function,
    .op_call     = tac_call,
    .op_return   = tac_return,
    .op_if       = tac_if,
    .op_else     = tac_else,
    .op_while    = tac_while,
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
            case TAC_CALL:  printf("CALL %" WORD_FMT "\n", instr->imm); break;
            case TAC_RET:   printf("RET\n"); break;
        }
    }
}

// --- Access TAC program ---
static inline tac_prog *tac_get_prog(VM *vm) {
    return &((tac_backend_state*)vm->user_data)->prog;
}

#endif // TAC_BACKEND_H
