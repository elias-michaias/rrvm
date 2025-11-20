#ifndef INTERP_H
#define INTERP_H

#include "../../vm/vm.h"

static inline word add_fn(word a, word b) {
    return a + b;
}

static inline word sub_fn(word a, word b) {
    return a - b;
}

static inline word mul_fn(word a, word b) {
    return a * b;
}

static inline word div_fn(word a, word b) {
    assert(b != 0 && "Division by zero");
    return a / b;
}

static inline void interp_push(VM *vm, word imm) {
    vm_push(vm, imm);
}

static inline void interp_move(VM *vm, word imm) {
    /* move tape pointer by imm (signed) */
    if (imm < 0) {
        size_t step = (size_t)(-imm);
        assert(vm->tp >= step && "Tape pointer underflow");
        vm->tp -= step;
    } else {
        vm->tp += (size_t)imm;
        assert(vm->tp < TAPE_SIZE && "Tape pointer overflow");
    }
}

static inline void interp_load(VM *vm) {
    vm_push(vm, vm->tape[vm->tp]);
}

static inline void interp_store(VM *vm) {
    word val = vm_pop(vm);
    vm->tape[vm->tp] = val;
}

static inline void interp_binary(VM *vm, word (*fn)(word, word)) {
    word a = vm_pop(vm);
    word b = vm_pop(vm);
    word result = fn(b, a);
    vm_push(vm, result);
}

static inline void interp_add(VM *vm) {
    interp_binary(vm, add_fn);
}

static inline void interp_sub(VM *vm) {
    interp_binary(vm, sub_fn);
}

static inline void interp_mul(VM *vm) {
    interp_binary(vm, mul_fn);
}

static inline void interp_div(VM *vm) {
    interp_binary(vm, div_fn);
}

static inline void interp_print(VM *vm) {
    word value = vm_pop(vm);
    printf("%" WORD_FMT "\n", value);
}

static inline void inter_setup(VM *vm) {}
static inline void inter_finalize(VM *vm, word imm) {}

static const Backend INTERPRETER_BACKEND = {
    .setup = inter_setup,
    .finalize = inter_finalize,
    .op_push = interp_push,
    .op_add = interp_add,
    .op_sub = interp_sub,
    .op_mul = interp_mul,
    .op_div = interp_div,
    .op_move = interp_move,
    .op_load = interp_load,
    .op_store = interp_store,
    .op_print = interp_print,
};

#endif // INTERP_H

