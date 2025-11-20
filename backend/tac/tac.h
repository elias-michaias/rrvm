#ifndef TAC_BACKEND_H
#define TAC_BACKEND_H

#include "../../vm/vm.h"
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>

// --- TAC IR ---
typedef enum {
    TAC_ADD,
    TAC_SUB,
    TAC_MUL,
    TAC_DIV,
    TAC_CONST,
    TAC_PRINT
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
typedef struct {
    tac_prog prog;
    int stack[STACK_SIZE];
    int sp;
    int next_temp;
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

static void tac_print(VM *vm) {
    tac_backend_state *s = tac_state(vm);
    assert(s->sp >= 1);
    int val = s->stack[--s->sp];
    tac_emit(&s->prog, (tac_instr){.op=TAC_PRINT, .lhs=val});
}

// --- TAC backend struct ---
static const Backend TAC_BACKEND = {
    .setup   = tac_setup,
    .finalize= tac_finalize,
    .op_push = tac_push,
    .op_add  = tac_add,
    .op_sub  = tac_sub,
    .op_mul  = tac_mul,
    .op_div  = tac_div,
    .op_print= tac_print,
};

// --- Dump TAC ---
static inline void tac_dump(const tac_prog *t) {
    for (size_t i = 0; i < t->count; i++) {
        const tac_instr *instr = &t->code[i];
        switch(instr->op) {
            case TAC_CONST: printf("t%d = " WORD_FMT, instr->dst, instr->imm); break;
            case TAC_ADD:   printf("t%d = t%d + t%d\n", instr->dst, instr->lhs, instr->rhs); break;
            case TAC_SUB:   printf("t%d = t%d - t%d\n", instr->dst, instr->lhs, instr->rhs); break;
            case TAC_MUL:   printf("t%d = t%d * t%d\n", instr->dst, instr->lhs, instr->rhs); break;
            case TAC_DIV:   printf("t%d = t%d / t%d\n", instr->dst, instr->lhs, instr->rhs); break;
            case TAC_PRINT: printf("PRINT t%d\n", instr->lhs); break;
        }
    }
}

// --- Access TAC program ---
static inline tac_prog *tac_get_prog(VM *vm) {
    return &((tac_backend_state*)vm->user_data)->prog;
}

#endif // TAC_BACKEND_H
