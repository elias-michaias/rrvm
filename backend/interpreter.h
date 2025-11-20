#ifndef INTERP_H
#define INTERP_H

#include "../vm/vm.h"

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
    printf("%i\n", value);
}

static const Backend INTERPRETER_BACKEND = {
    .op_push = interp_push,
    .op_add = interp_add,
    .op_sub = interp_sub,
    .op_mul = interp_mul,
    .op_div = interp_div,
    .op_print = interp_print,
};

#endif // INTERP_H

