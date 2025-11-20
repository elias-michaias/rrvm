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
    OP_HALT,
} OpCode;

typedef struct {
    const word *code;
    size_t code_len;
    size_t ip;
    word stack[STACK_SIZE];
    int sp;

    /* tape and pointer */
    word tape[TAPE_SIZE];
    int tp;

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
    memset(vm->tape, 0, sizeof(vm->tape));

    while (vm->ip < vm->code_len) {
        OpCode op = (OpCode)vm->code[vm->ip++];

        switch (op) {
            case OP_NOP:
                break;
            case OP_PUSH: {
                assert(vm->ip < vm->code_len && "Unexpected end of code");
                word imm = vm->code[vm->ip++];
                backend->op_push(vm, imm);
                break;
            }
            case OP_ADD:
                backend->op_add(vm);
                break;
            case OP_SUB:
                backend->op_sub(vm);
                break;
            case OP_MUL:
                backend->op_mul(vm);
                break;
            case OP_DIV:
                backend->op_div(vm);
                break;
            case OP_MOVE: {
                assert(vm->ip < vm->code_len && "Unexpected end of code");
                word imm = vm->code[vm->ip++];
                backend->op_move(vm, imm);
                break;
            }
            case OP_LOAD:
                backend->op_load(vm);
                break;
            case OP_STORE:
                backend->op_store(vm);
                break;
            case OP_PRINT:
                backend->op_print(vm);
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

#define __end VM vm = { .code = prog, .code_len = p, .ip = 0, .sp = 0, }; return vm;

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

#endif // VM_H
