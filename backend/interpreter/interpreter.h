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

static inline void interp_push(VM *vm, int type, word imm) {
    /* push a value and record its TypeTag in the parallel types array */
    vm_push(vm, imm);
    vm->types[vm->sp - 1] = (TypeTag)type;
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
    /* push both value and its recorded tape type */
    vm_push(vm, vm->tape[vm->tp]);
    vm->types[vm->sp - 1] = vm->tape_types[vm->tp];
}

static inline void interp_store(VM *vm) {
    /* pop value and propagate its type into the tape cell */
    assert(vm->sp > 0 && "interp_store: empty stack");
    TypeTag t = vm->types[vm->sp - 1];
    word val = vm_pop(vm);
    vm->tape[vm->tp] = val;
    vm->tape_types[vm->tp] = t;
}

static inline void interp_binary(VM *vm, word (*fn)(word, word)) {
    /* binary ops must have two operands with identical types. The VM assumes
       well-typed input; mismatch causes an assert (fail-fast). */
    assert(vm->sp >= 2 && "interp_binary: stack underflow");
    TypeTag top = vm->types[vm->sp - 1];
    TypeTag next = vm->types[vm->sp - 2];
    assert(top == next && "interp_binary: type mismatch");
    word a = vm_pop(vm); /* top */
    word b = vm_pop(vm); /* next */
    word result = fn(b, a);
    vm_push(vm, result);
    vm->types[vm->sp - 1] = top;
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
    assert(vm->sp > 0 && "interp_print: empty stack");
    TypeTag t = vm->types[vm->sp - 1];
    word value = vm_pop(vm);

    switch (t) {
        case TYPE_F32: {
            uint32_t bits = (uint32_t)(value & 0xFFFFFFFFu);
            float f;
            memcpy(&f, &bits, sizeof(f));
            /* upcast to double for consistent printf formatting */
            printf("%f\n", (double)f);
            break;
        }
        case TYPE_F64: {
            double d;
            memcpy(&d, &value, sizeof(d));
            printf("%f\n", d);
            break;
        }
        case TYPE_U8:
        case TYPE_U16:
        case TYPE_U32:
        case TYPE_U64: {
            uint64_t u = (uint64_t)value;
            printf("%" PRIu64 "\n", u);
            break;
        }
        case TYPE_BOOL:
        case TYPE_PTR:
        case TYPE_I8:
        case TYPE_I16:
        case TYPE_I32:
        case TYPE_I64:
        default: {
            int64_t s = (int64_t)value;
            printf("%" WORD_FMT "\n", s);
            break;
        }
    }
}

static inline void inter_setup(VM *vm) { /* nothing for now */ }

static inline void inter_finalize(VM *vm, word imm) { /* nothing */ }

/* function recording: record function start ip */
static inline void interp_function(VM *vm, word func_index) {
    if ((size_t)func_index >= sizeof(vm->functions)/sizeof(vm->functions[0])) return;
    /* record the function's start ip */
    vm->functions[func_index] = vm->ip;
    if (vm->functions_count <= (size_t)func_index) vm->functions_count = (size_t)func_index + 1;
    /* skip over the function body at runtime until matching ENDBLOCK */
    size_t depth = 0;
    size_t i = vm->ip;
    while (i < vm->code_len) {
        OpCode op = (OpCode)vm->code[i++];
        if (op == OP_FUNCTION || op == OP_IF || op == OP_WHILE) {
            depth++;
        } else if (op == OP_ENDBLOCK && depth == 0) {
            vm->ip = i; /* position after ENDBLOCK */
            return;
        } else if (op == OP_ELSE || op == OP_ENDBLOCK) {
            depth--;
        } else {
            /* skip immediates: OP_PUSH and OP_SET carry two immediates (type+imm).
               OP_MOVE, OP_FUNCTION, OP_CALL, OP_WHILE carry one immediate. */
            if (op == OP_PUSH || op == OP_SET) i += 2;
            else if (op == OP_MOVE || op == OP_FUNCTION || op == OP_CALL || op == OP_WHILE) i++;
        }
    }
    vm->ip = vm->code_len; /* fallback */
}

static inline void interp_call(VM *vm, word func_index) {
    assert((size_t)vm->call_sp < CALL_STACK_SIZE && "call stack overflow");
    size_t fi = (size_t)func_index;
    assert(fi < vm->functions_count && "call to unknown function index");
    /* push return ip and old fp */
    vm->call_stack[vm->call_sp].return_ip = vm->ip;
    vm->call_stack[vm->call_sp].old_fp = vm->fp;
    vm->call_sp++;
    /* new frame begins at current sp */
    vm->fp = vm->sp;
    /* jump to function start */
    vm->ip = vm->functions[fi];
}

static inline void interp_return(VM *vm) {
    assert(vm->call_sp > 0 && "return with empty call stack");
    word ret = 0;
    /* if there's a return value on the stack, pop it */
    if (vm->sp > vm->fp) ret = vm_pop(vm);
    /* restore frame and return ip */
    vm->call_sp--;
    size_t ret_ip = vm->call_stack[vm->call_sp].return_ip;
    int old_fp = vm->call_stack[vm->call_sp].old_fp;
    /* tear down locals */
    vm->sp = vm->fp;
    vm->fp = old_fp;
    vm->ip = ret_ip;
    /* push return value */
    vm_push(vm, ret);
}

/* simple block stack entry is defined in vm/vm.h */

static inline void interp_if(VM *vm) {
    word cond = vm_pop(vm);
    if (cond == 0) {
        /* skip to matching ELSE or ENDBLOCK */
        size_t depth = 0;
        size_t i = vm->ip;
        while (i < vm->code_len) {
            OpCode op = (OpCode)vm->code[i++];
            if (op == OP_IF || op == OP_WHILE) {
                depth++;
            } else if (op == OP_ELSE && depth == 0) {
                vm->ip = i; /* execute after ELSE */
                return;
            } else if (op == OP_ENDBLOCK && depth == 0) {
                vm->ip = i; /* execute after ENDBLOCK */
                return;
            } else if (op == OP_ELSE || op == OP_ENDBLOCK) {
                depth--;
            } else {
                /* if op has immediate, skip it. account for new PUSH/SET formats. */
                if (op == OP_PUSH || op == OP_SET) i += 2;
                else if (op == OP_MOVE || op == OP_FUNCTION || op == OP_CALL || op == OP_WHILE) i++;
            }
        }
        /* not found -> end execution */
        vm->ip = vm->code_len;
    } else {
        /* enter if: push a marker onto block stack */
        assert(vm->block_sp < 256 && "block stack overflow");
        vm->block_stack[vm->block_sp].type = OP_IF;
        vm->block_stack[vm->block_sp].ip = vm->ip;
        vm->block_sp++;
    }
}

static inline void interp_else(VM *vm) {
    /* skip to matching ENDBLOCK */
    size_t depth = 0;
    size_t i = vm->ip;
    while (i < vm->code_len) {
        OpCode op = (OpCode)vm->code[i++];
        if (op == OP_IF || op == OP_WHILE) {
            depth++;
        } else if (op == OP_ENDBLOCK && depth == 0) {
            vm->ip = i; /* execute after ENDBLOCK */
            /* pop IF marker if present */
            if (vm->block_sp > 0) vm->block_sp--;
            return;
        } else if (op == OP_ELSE || op == OP_ENDBLOCK) {
            depth--;
        } else {
            /* skip immediates, accounting for typed PUSH/SET */
            if (op == OP_PUSH || op == OP_SET) i += 2;
            else if (op == OP_MOVE || op == OP_FUNCTION || op == OP_CALL || op == OP_WHILE) i++;
        }
    }
    vm->ip = vm->code_len;
}

static inline void interp_endblock(VM *vm) {
    /* if top of block stack is WHILE, loop back to its ip; otherwise pop marker */
    if (vm->block_sp > 0) {
        int top = vm->block_sp - 1;
        if (vm->block_stack[top].type == OP_WHILE) {
            /* loop back to condition: set ip to stored ip */
            vm->ip = vm->block_stack[top].ip;
        } else {
            vm->block_sp--;
        }
    }
}

static inline void interp_while(VM *vm, word cond_ip) {
    /* robust while: the cond_ip immediate points to the first instruction that
       computes the loop condition (may be multiple ops). The condition code has
       already executed, so pop its result and decide. If non-zero, push a WHILE
       marker storing cond_ip so ENDBLOCK can jump back to re-evaluate; otherwise
       skip the loop body. */
    word cond = vm_pop(vm);
    if (cond == 0) {
        /* skip to ENDBLOCK */
        size_t depth = 0;
        size_t i = vm->ip;
        while (i < vm->code_len) {
            OpCode op = (OpCode)vm->code[i++];
            if (op == OP_IF || op == OP_WHILE) {
                depth++;
            } else if (op == OP_ENDBLOCK && depth == 0) {
                vm->ip = i;
                return;
            } else if (op == OP_ELSE || op == OP_ENDBLOCK) {
                depth--;
            } else {
                /* typed PUSH/SET use two immediates; other ops use one */
                if (op == OP_PUSH || op == OP_SET) i += 2;
                else if (op == OP_MOVE || op == OP_FUNCTION || op == OP_CALL || op == OP_WHILE) i++;
            }
        }
        vm->ip = vm->code_len;
    } else {
        /* enter loop: push WHILE marker that stores the cond_ip provided by the emitter */
        assert(vm->block_sp < 256 && "block stack overflow");
        vm->block_stack[vm->block_sp].type = OP_WHILE;
        vm->block_stack[vm->block_sp].ip = (size_t)cond_ip;
        vm->block_sp++;
    }
}

static inline void interp_deref(VM *vm) {
    /* push current tp and set tp = tape[tp] (pointer chase) */
    vm_push_tp(vm, vm->tp);
    word new_tp = vm->tape[vm->tp];
    assert(new_tp >= 0 && (size_t)new_tp < TAPE_SIZE && "DEREF produced invalid tape index");
    vm->tp = (int)new_tp;
}

static inline void interp_refer(VM *vm) {
    /* pop previous tp from the pointer stack and restore */
    vm->tp = vm_pop_tp(vm);
}

static inline void interp_where(VM *vm) {
    /* push the current tp value onto the data stack */
    vm_push(vm, (word)vm->tp);
}

static inline void interp_offset(VM *vm, word imm) {
    /* adjust the tape pointer by signed immediate */
    if (imm < 0) {
        size_t step = (size_t)(-imm);
        assert(vm->tp >= (int)step && "OFFSET underflow");
        vm->tp -= (int)step;
    } else {
        vm->tp += (size_t)imm;
        assert((size_t)vm->tp < TAPE_SIZE && "OFFSET overflow");
    }
}

static inline void interp_index(VM *vm) {
    /* shift pointer by the value stored at tape[tp] */
    word delta = vm->tape[vm->tp];
    if (delta < 0) {
        size_t step = (size_t)(-delta);
        assert(vm->tp >= (int)step && "INDEX underflow");
        vm->tp -= (int)step;
    } else {
        vm->tp += (size_t)delta;
        assert((size_t)vm->tp < TAPE_SIZE && "INDEX overflow");
    }
}

static inline void interp_set(VM *vm, int type, word imm) {
    /* store immediate into tape at current tp and record its type */
    vm->tape[vm->tp] = imm;
    vm->tape_types[vm->tp] = (TypeTag)type;
}

/* --- NEW scalar multi/bitwise/logical handlers (stack-oriented) --- */

/* remainder: pops two values a,b and pushes b % a */
static inline word rem_fn(word a, word b) {
    (void)a; (void)b; /* keep signature consistent with interp_binary usage */
    /* interp_binary will call rem_fn(b,a) so implement as a % b in that order */
    return 0; /* placeholder, never called directly */
}

static inline word rem_impl(word a, word b) {
    assert(b != 0 && "Modulo by zero");
    return a % b;
}

static inline void interp_rem(VM *vm) {
    interp_binary(vm, rem_impl);
}

/* logical OR/AND as binary ops producing 0/1 */
static inline word or_impl(word a, word b) { return (a || b) ? 1 : 0; }
static inline word and_impl(word a, word b) { return (a && b) ? 1 : 0; }
static inline void interp_orassign(VM *vm) { interp_binary(vm, or_impl); }
static inline void interp_andassign(VM *vm) { interp_binary(vm, and_impl); }

/* NOT: unary */
static inline void interp_not(VM *vm) {
    word v = vm_pop(vm);
    vm_push(vm, v ? 0 : 1);
}

/* bitwise binary ops */
static inline word bitand_impl(word a, word b) { return a & b; }
static inline word bitor_impl(word a, word b) { return a | b; }
static inline word bitxor_impl(word a, word b) { return a ^ b; }
static inline void interp_bitand(VM *vm) { interp_binary(vm, bitand_impl); }
static inline void interp_bitor(VM *vm) { interp_binary(vm, bitor_impl); }
static inline void interp_bitxor(VM *vm) { interp_binary(vm, bitxor_impl); }

/* shifts: a << b, logical/right zero-extend, arithmetic/right sign-extend */
static inline word lsh_impl(word a, word b) { return a << b; }
static inline word lrsh_impl(word a, word b) { return (word)(((uint64_t)a) >> (uint64_t)b); }
static inline word arsh_impl(word a, word b) { return a >> b; }
static inline void interp_lsh(VM *vm) { interp_binary(vm, lsh_impl); }
static inline void interp_lrsh(VM *vm) { interp_binary(vm, lrsh_impl); }
static inline void interp_arsh(VM *vm) { interp_binary(vm, arsh_impl); }

/* GEZ: unary test */
static inline void interp_gez(VM *vm) {
    word v = vm_pop(vm);
    vm_push(vm, v >= 0 ? 1 : 0);
}

static const Backend __INTERPRETER = {
    .setup = inter_setup,
    .finalize = inter_finalize,
    .op_push = interp_push,
    .op_add = interp_add,
    .op_sub = interp_sub,
    .op_mul = interp_mul,
    .op_div = interp_div,
    .op_rem = interp_rem,
    .op_move = interp_move,
    .op_load = interp_load,
    .op_store = interp_store,
    .op_print = interp_print,

    /* pointer/reference hooks */
    .op_deref = interp_deref,
    .op_refer = interp_refer,
    .op_where = interp_where,
    .op_offset = interp_offset,
    .op_index = interp_index,
    .op_set = interp_set,

    .op_function = interp_function,
    .op_call     = interp_call,
    .op_return   = interp_return,
    .op_while    = interp_while,
    .op_if       = interp_if,
    .op_else     = interp_else,
    .op_endblock = interp_endblock,

    /* new multi-element / bitwise / logical hooks */
    .op_orassign = interp_orassign,
    .op_andassign = interp_andassign,
    .op_not = interp_not,
    .op_bitand = interp_bitand,
    .op_bitor = interp_bitor,
    .op_bitxor = interp_bitxor,
    .op_lsh = interp_lsh,
    .op_lrsh = interp_lrsh,
    .op_arsh = interp_arsh,
    .op_gez = interp_gez,
};

#endif // INTERP_H

