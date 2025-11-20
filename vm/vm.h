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
    OP_MOVE,
    OP_LOAD,
    OP_STORE,
    OP_PRINT,

    /* control-flow / functions */
    OP_FUNCTION, /* followed by function index immediate */
    OP_CALL,     /* followed by function index immediate */
    OP_RETURN,
    OP_WHILE,
    OP_IF,
    OP_ELSE,
    OP_ENDBLOCK,

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
    void (*op_move)(VM *vm, word imm);
    void (*op_load)(VM *vm);
    void (*op_store)(VM *vm);
    void (*op_print)(VM *vm);

    /* control/call hooks (optional) */
    void (*op_function)(VM *vm, word func_index);
    void (*op_call)(VM *vm, word func_index);
    void (*op_return)(VM *vm);
    void (*op_while)(VM *vm);
    void (*op_if)(VM *vm);
    void (*op_else)(VM *vm);
    void (*op_endblock)(VM *vm);
} Backend;

static inline void vm_push(VM *vm, word imm) {
    assert(vm->sp < STACK_SIZE && "Stack overflow");
    vm->stack[vm->sp++] = imm;
}

static inline word vm_pop(VM *vm) {
    assert(vm->sp > 0 && "Stack underflow");
    return vm->stack[--vm->sp];
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

            case OP_WHILE:
                if (backend->op_while) backend->op_while(vm);
                break;
            case OP_IF:
                if (backend->op_if) backend->op_if(vm);
                break;
            case OP_ELSE:
                if (backend->op_else) backend->op_else(vm);
                break;
            case OP_ENDBLOCK:
                if (backend->op_endblock) backend->op_endblock(vm);
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

#define __fin VM vm = { .code = prog, .code_len = p, .ip = 0, .sp = 0, }; return vm;

// helpers for each op
#define __push(imm) p = emit1(prog, p, OP_PUSH, imm)
#define __add()    p = emit0(prog, p, OP_ADD)
#define __sub()    p = emit0(prog, p, OP_SUB)
#define __mul()    p = emit0(prog, p, OP_MUL)
#define __div()    p = emit0(prog, p, OP_DIV)
#define __move(imm) p = emit1(prog, p, OP_MOVE, imm)
#define __load()  p = emit0(prog, p, OP_LOAD)
#define __store() p = emit0(prog, p, OP_STORE)
#define __print()  p = emit0(prog, p, OP_PRINT)
#define __halt()   p = emit0(prog, p, OP_HALT)

/* function / control flow emit helpers */
#define __func(idx) p = emit1(prog, p, OP_FUNCTION, idx)
#define __call(idx)     p = emit1(prog, p, OP_CALL, idx)
#define __ret()      p = emit0(prog, p, OP_RETURN)
#define __while_()       p = emit0(prog, p, OP_WHILE)
#define __if()          p = emit0(prog, p, OP_IF)
#define __else()        p = emit0(prog, p, OP_ELSE)
#define __end()        p = emit0(prog, p, OP_ENDBLOCK)

#endif // VM_H
