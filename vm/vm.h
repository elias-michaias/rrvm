#ifndef VM_H
#define VM_H

#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include <stdlib.h>
#include <string.h>

// vm config
#ifndef STACK_SIZE
#define STACK_SIZE 1024
#endif

#ifndef WORD_SIZE
#define WORD_SIZE int64_t
#endif

typedef WORD_SIZE word;

typedef enum {
    OP_NOP = 0,
    OP_PUSH,
    OP_ADD,
    OP_SUB,
    OP_MUL,
    OP_DIV,
    OP_PRINT,
    OP_HALT,
    // OP_LOAD,
    // OP_STORE,
    // OP_JMP,
    // OP_JZ,
} OpCode;

typedef struct {
    const word *code;
    size_t code_len;
    size_t ip;
    word stack[STACK_SIZE];
    int sp;
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
            case OP_PRINT:
                backend->op_print(vm);
                break;
            case OP_HALT:
                break;
            default:
                fprintf(stderr, "Unknown opcode: %d\n", op);
                exit(1);
        }
    }

}

// helpers for setup
#define __init(size) \
    word prog[size]; \
    size_t p = 0;

#define __end VM vm = { .code = prog, .code_len = p, .ip = 0, .sp = 0, }; return vm;

// helpers for each op
#define __push(imm) p = emit1(prog, p, OP_PUSH, imm)
#define __add()    p = emit0(prog, p, OP_ADD)
#define __sub()    p = emit0(prog, p, OP_SUB)
#define __mul()    p = emit0(prog, p, OP_MUL)
#define __div()    p = emit0(prog, p, OP_DIV)
#define __print()  p = emit0(prog, p, OP_PRINT)
#define __halt()   p = emit0(prog, p, OP_HALT)

#endif // VM_H
