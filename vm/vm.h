#ifndef VM_H
#define VM_H

#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>

// vm config
#ifndef STACK_SIZE
#define STACK_SIZE 1024
#endif

#ifndef TAPE_SIZE
#define TAPE_SIZE 1024
#endif

#ifndef WORD_BITS
#define WORD_BITS 64
#endif

#ifndef CALL_STACK_SIZE
#define CALL_STACK_SIZE 256
#endif

#include <stdint.h>
#include <inttypes.h>

#if WORD_BITS == 64
typedef int64_t word;
#define WORD_FMT PRId64
#elif WORD_BITS == 32
typedef int32_t word;
#define WORD_FMT PRId32
#else
#error "WORD_BITS must be 32 or 64"
#endif

typedef enum {
    OP_NOP = 0,
    OP_PUSH,
    OP_ADD,
    OP_SUB,
    OP_MUL,
    OP_DIV,
    OP_REM,
    OP_MOVE,
    OP_LOAD,
    OP_STORE,
    OP_PRINT,

    /* pointer/reference related */
    OP_DEREF, /* pointer-chase: set tp = tape[tp], push old tp to tp_stack */
    OP_REFER, /* pop tp_stack and restore tp */
    OP_WHERE, /* push current tp onto stack */
    OP_OFFSET, /* followed by immediate offset */
    OP_INDEX, /* shift pointer by value under pointer (tape[tp]) */
    OP_SET,   /* set register(s) to immediate (distinct from PUSH if desired) */

    /* control-flow / functions */
    OP_FUNCTION, /* followed by function index immediate */
    OP_CALL,     /* followed by function index immediate */
    OP_RETURN,
    OP_WHILE,
    OP_IF,
    OP_ELSE,
    OP_ENDBLOCK,

    /* new multi-element / bitwise / logical ops (each takes a single immediate N) */
    OP_ORASSign, /* logical OR-assign: reg[i] ||= tape_ptr[i] */
    OP_ANDASSign, /* logical AND-assign: reg[i] &&= tape_ptr[i] */
    OP_NOT,      /* logical NOT: reg[i] = !reg[i] (takes N) */
    OP_BITAND,   /* bitwise AND: reg[i] &= tape_ptr[i] */
    OP_BITOR,    /* bitwise OR: reg[i] |= tape_ptr[i] */
    OP_BITXOR,   /* bitwise XOR: reg[i] ^= tape_ptr[i] */
    OP_LSH,      /* left shift: reg[i] <<= tape_ptr[i] */
    OP_LRSH,     /* logical right shift: reg[i] = (uint64_t)reg[i] >> tape_ptr[i] */
    OP_ARSH,     /* arithmetic right shift: reg[i] >>= tape_ptr[i] */
    OP_GEZ,      /* greater-or-equal-zero: reg[i] = reg[i] >= 0 */

    OP_HALT,
} OpCode;

/* block stack entry used by interpreter for structured blocks */
typedef struct { OpCode type; size_t ip; } block_entry;

typedef struct {
    const word *code;
    size_t code_len;
    size_t ip;
    word stack[STACK_SIZE];
    int sp;

    /* tape and pointer */
    word tape[TAPE_SIZE];
    int tp;

    /* pointer stack to support nested deref/refer */
    int tp_stack[TAPE_SIZE];
    int tp_sp;

    /* simple function table (function index -> code ip) */
    size_t functions[256];
    size_t functions_count;

    /* simple call-stack: stores return ip and old frame pointer */
    struct {
        size_t return_ip;
        int old_fp;
    } call_stack[CALL_STACK_SIZE];
    int call_sp;

    /* frame pointer (index into stack for locals) */
    int fp;

    /* block stack for IF/ELSE/WHILE/ENDBLOCK handling */
    block_entry block_stack[256];
    int block_sp;

    void *user_data; // backend-specific data
} VM;

typedef struct Backend {
    void (*setup)(VM *vm);
    void (*finalize)(VM *vm, word imm);
    void (*op_push)(VM *vm, word imm);
    void (*op_add)(VM *vm);
    void (*op_sub)(VM *vm);
    void (*op_mul)(VM *vm);
    void (*op_div)(VM *vm);
    /* REM and the new ops are scalar (no immediate) */
    void (*op_rem)(VM *vm);
    void (*op_move)(VM *vm, word imm);
    void (*op_load)(VM *vm);
    void (*op_store)(VM *vm);
    void (*op_print)(VM *vm);

    /* pointer/reference hooks */
    void (*op_deref)(VM *vm);
    void (*op_refer)(VM *vm);
    void (*op_where)(VM *vm);
    void (*op_offset)(VM *vm, word imm);
    void (*op_index)(VM *vm);
    void (*op_set)(VM *vm, word imm);

    /* control/call hooks (optional) */
    void (*op_function)(VM *vm, word func_index);
    void (*op_call)(VM *vm, word func_index);
    void (*op_return)(VM *vm);
    /* op_while still receives an immediate indicating the ip of the condition's first instruction */
    void (*op_while)(VM *vm, word cond_ip);
    void (*op_if)(VM *vm);
    void (*op_else)(VM *vm);
    void (*op_endblock)(VM *vm);

    /* new multi-element / bitwise / logical hooks (scalar versions) */
    void (*op_orassign)(VM *vm);
    void (*op_andassign)(VM *vm);
    void (*op_not)(VM *vm);
    void (*op_bitand)(VM *vm);
    void (*op_bitor)(VM *vm);
    void (*op_bitxor)(VM *vm);
    void (*op_lsh)(VM *vm);
    void (*op_lrsh)(VM *vm);
    void (*op_arsh)(VM *vm);
    void (*op_gez)(VM *vm);
} Backend;

static inline void vm_push(VM *vm, word imm) {
    assert(vm->sp < STACK_SIZE && "Stack overflow");
    vm->stack[vm->sp++] = imm;
}

static inline word vm_pop(VM *vm) {
    assert(vm->sp > 0 && "Stack underflow");
    return vm->stack[--vm->sp];
}

/* pointer-stack helpers */
static inline void vm_push_tp(VM *vm, int tp_val) {
    assert(vm->tp_sp < TAPE_SIZE && "pointer stack overflow");
    vm->tp_stack[vm->tp_sp++] = tp_val;
}

static inline int vm_pop_tp(VM *vm) {
    assert(vm->tp_sp > 0 && "pointer stack underflow");
    return vm->tp_stack[--vm->tp_sp];
}

static inline size_t emit0(word *buf, size_t pos, word op) {
    buf[pos++] = op;
    return pos;
}

static inline size_t emit1(word *buf, size_t pos, word op, word imm) {
    buf[pos++] = op;
    buf[pos++] = imm;
    return pos;
}

static inline void run_vm(VM *vm, const Backend *backend) {

    if (backend->setup) backend->setup(vm);

    vm->ip = 0;
    vm->sp = 0;
    vm->tp = 0;
    vm->tp_sp = 0;
    vm->call_sp = 0;
    vm->fp = 0;
    vm->functions_count = 0;
    vm->block_sp = 0;
    memset(vm->tape, 0, sizeof(vm->tape));

    while (vm->ip < vm->code_len) {
        OpCode op = (OpCode)vm->code[vm->ip++];

        switch (op) {
            case OP_NOP:
                break;
            case OP_PUSH: {
                assert(vm->ip < vm->code_len && "Unexpected end of code");
                word imm = vm->code[vm->ip++];
                if (backend->op_push) backend->op_push(vm, imm);
                break;
            }
            case OP_ADD:
                if (backend->op_add) backend->op_add(vm);
                break;
            case OP_SUB:
                if (backend->op_sub) backend->op_sub(vm);
                break;
            case OP_MUL:
                if (backend->op_mul) backend->op_mul(vm);
                break;
            case OP_DIV:
                if (backend->op_div) backend->op_div(vm);
                break;
            case OP_REM:
                if (backend->op_rem) backend->op_rem(vm);
                break;
            case OP_MOVE: {
                assert(vm->ip < vm->code_len && "Unexpected end of code");
                word imm = vm->code[vm->ip++];
                if (backend->op_move) backend->op_move(vm, imm);
                break;
            }
            case OP_LOAD:
                if (backend->op_load) backend->op_load(vm);
                break;
            case OP_STORE:
                if (backend->op_store) backend->op_store(vm);
                break;
            case OP_PRINT:
                if (backend->op_print) backend->op_print(vm);
                break;

            case OP_DEREF:
                if (backend->op_deref) backend->op_deref(vm);
                break;
            case OP_REFER:
                if (backend->op_refer) backend->op_refer(vm);
                break;
            case OP_WHERE:
                if (backend->op_where) backend->op_where(vm);
                break;
            case OP_OFFSET: {
                assert(vm->ip < vm->code_len && "Unexpected end of code");
                word imm = vm->code[vm->ip++];
                if (backend->op_offset) backend->op_offset(vm, imm);
                break;
            }
            case OP_INDEX:
                if (backend->op_index) backend->op_index(vm);
                break;
            case OP_SET: {
                assert(vm->ip < vm->code_len && "Unexpected end of code");
                word imm = vm->code[vm->ip++];
                if (backend->op_set) backend->op_set(vm, imm);
                break;
            }

            case OP_FUNCTION: {
                assert(vm->ip < vm->code_len && "Unexpected end of code");
                word func_idx = vm->code[vm->ip++];
                /* backends may record function boundaries; otherwise ignore at runtime */
                if (backend->op_function) backend->op_function(vm, func_idx);
                break;
            }

            case OP_CALL: {
                assert(vm->ip < vm->code_len && "Unexpected end of code");
                word func_idx = vm->code[vm->ip++];
                if (backend->op_call) backend->op_call(vm, func_idx);
                break;
            }

            case OP_RETURN:
                if (backend->op_return) backend->op_return(vm);
                break;

            case OP_WHILE: {
                assert(vm->ip < vm->code_len && "Unexpected end of code");
                word cond_ip = vm->code[vm->ip++];
                if (backend->op_while) backend->op_while(vm, cond_ip);
                break;
            }
            case OP_IF:
                if (backend->op_if) backend->op_if(vm);
                break;
            case OP_ELSE:
                if (backend->op_else) backend->op_else(vm);
                break;
            case OP_ENDBLOCK:
                if (backend->op_endblock) backend->op_endblock(vm);
                break;

            /* scalar multi/bitwise/logical op handlers (no immediates) */
            case OP_ORASSign:
                if (backend->op_orassign) backend->op_orassign(vm);
                break;
            case OP_ANDASSign:
                if (backend->op_andassign) backend->op_andassign(vm);
                break;
            case OP_NOT:
                if (backend->op_not) backend->op_not(vm);
                break;
            case OP_BITAND:
                if (backend->op_bitand) backend->op_bitand(vm);
                break;
            case OP_BITOR:
                if (backend->op_bitor) backend->op_bitor(vm);
                break;
            case OP_BITXOR:
                if (backend->op_bitxor) backend->op_bitxor(vm);
                break;
            case OP_LSH:
                if (backend->op_lsh) backend->op_lsh(vm);
                break;
            case OP_LRSH:
                if (backend->op_lrsh) backend->op_lrsh(vm);
                break;
            case OP_ARSH:
                if (backend->op_arsh) backend->op_arsh(vm);
                break;
            case OP_GEZ:
                if (backend->op_gez) backend->op_gez(vm);
                break;

            case OP_HALT:
                return;
            default:
                fprintf(stderr, "Unknown opcode: %d\n", op);
                exit(1);
        }
    }

}

// helpers for setup
#define __init(size) \
    static word prog[size]; \
    size_t p = 0;

#define __fin VM vm = { .code = prog, .code_len = p, .ip = 0, .sp = 0, }; return vm

// helpers for each op
#define __push(imm) p = emit1(prog, p, OP_PUSH, imm)
#define __add    p = emit0(prog, p, OP_ADD)
#define __sub    p = emit0(prog, p, OP_SUB)
#define __mul    p = emit0(prog, p, OP_MUL)
#define __div    p = emit0(prog, p, OP_DIV)
#define __rem p = emit0(prog, p, OP_REM)
#define __move(imm) p = emit1(prog, p, OP_MOVE, imm)
#define __load  p = emit0(prog, p, OP_LOAD)
#define __store p = emit0(prog, p, OP_STORE)
#define __print  p = emit0(prog, p, OP_PRINT)
#define __halt   p = emit0(prog, p, OP_HALT)

// pointer/reference emit helpers
#define __deref p = emit0(prog, p, OP_DEREF)
#define __refer p = emit0(prog, p, OP_REFER)
#define __where p = emit0(prog, p, OP_WHERE)
#define __offset(imm) p = emit1(prog, p, OP_OFFSET, imm)
#define __index p = emit0(prog, p, OP_INDEX)
#define __set(imm) p = emit1(prog, p, OP_SET, imm)

/* function / control flow emit helpers */
#define __func(idx) p = emit1(prog, p, OP_FUNCTION, idx)
#define __call(idx)     p = emit1(prog, p, OP_CALL, idx)
#define __ret      p = emit0(prog, p, OP_RETURN)
#define __label(name) size_t __lbl_##name = p
#define __while(name) p = emit1(prog, p, OP_WHILE, (word)__lbl_##name)
#define __if          p = emit0(prog, p, OP_IF)
#define __else        p = emit0(prog, p, OP_ELSE)
#define __end        p = emit0(prog, p, OP_ENDBLOCK)

/* emit helpers for new multi-element / bitwise / logical ops (stack-oriented, no immediate) */
#define __orass    p = emit0(prog, p, OP_ORASSign)
#define __andass   p = emit0(prog, p, OP_ANDASSign)
#define __not      p = emit0(prog, p, OP_NOT)
#define __bitand   p = emit0(prog, p, OP_BITAND)
#define __bitor    p = emit0(prog, p, OP_BITOR)
#define __bitxor   p = emit0(prog, p, OP_BITXOR)
#define __lsh      p = emit0(prog, p, OP_LSH)
#define __lrsh     p = emit0(prog, p, OP_LRSH)
#define __arsh     p = emit0(prog, p, OP_ARSH)
#define __gez      p = emit0(prog, p, OP_GEZ)

#endif // VM_H
