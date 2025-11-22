#ifndef VM_H
#define VM_H

#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>

/* VM configuration */
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

/* Primitive type tags for stack/tape values.
 *
 * The VM is strict: it assumes input programs are well-typed. The interpreter
 * and TAC lowering will assert on type mismatches. Values are stored in the
 * existing `word` slots; narrower integer types and floats are represented as
 * bit patterns in that storage and interpreted according to the TypeTag.
 */
typedef enum {
    TYPE_UNKNOWN = 0,
    TYPE_I8,    /* signed 8-bit */
    TYPE_U8,    /* unsigned 8-bit (char) */
    TYPE_I16,   /* signed 16-bit */
    TYPE_U16,   /* unsigned 16-bit */
    TYPE_I32,   /* signed 32-bit */
    TYPE_U32,   /* unsigned 32-bit */
    TYPE_I64,   /* signed 64-bit */
    TYPE_U64,   /* unsigned 64-bit */
    TYPE_F32,   /* 32-bit float (bit-cast into word) */
    TYPE_F64,   /* 64-bit double (bit-cast into word) */
    TYPE_BOOL,
    TYPE_PTR,   /* pointer/tape index */
    TYPE_VOID,
} TypeTag;

/* VM opcodes */
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
    OP_ORASSign,
    OP_ANDASSign,
    OP_NOT,
    OP_BITAND,
    OP_BITOR,
    OP_BITXOR,
    OP_LSH,
    OP_LRSH,
    OP_ARSH,
    OP_GEZ,

    OP_HALT,
} OpCode;

/* block stack entry used by interpreter for structured blocks */
typedef struct { OpCode type; size_t ip; } block_entry;

/* VM state */
typedef struct {
    const word *code;
    size_t code_len;
    size_t ip;

    /* data stack */
    word stack[STACK_SIZE];
    int sp;

    /* parallel type array for data stack */
    TypeTag types[STACK_SIZE];

    /* tape and pointer */
    word tape[TAPE_SIZE];
    TypeTag tape_types[TAPE_SIZE];
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

    void *user_data; /* backend-specific data */
} VM;

/* Backend hooks. op_push and op_set now receive a TypeTag (as int) along with the immediate. */
typedef struct Backend {
    void (*setup)(VM *vm);
    void (*finalize)(VM *vm, word imm);

    /* push receives (vm, type, imm) */
    void (*op_push)(VM *vm, int type, word imm);

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

    /* set receives (vm, type, imm) */
    void (*op_set)(VM *vm, int type, word imm);

    /* control/call hooks */
    void (*op_function)(VM *vm, word func_index);
    void (*op_call)(VM *vm, word func_index);
    void (*op_return)(VM *vm);
    void (*op_while)(VM *vm, word cond_ip);
    void (*op_if)(VM *vm);
    void (*op_else)(VM *vm);
    void (*op_endblock)(VM *vm);

    /* new multi-element / bitwise / logical hooks */
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

/* simple stack helpers */
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

/* emit helpers for building programs (in the sample program files) */
static inline size_t emit0(word *buf, size_t pos, word op) {
    buf[pos++] = op;
    return pos;
}

static inline size_t emit1(word *buf, size_t pos, word op, word imm) {
    buf[pos++] = op;
    buf[pos++] = imm;
    return pos;
}

/* emit helper for ops that have two immediates (type + imm) */
static inline size_t emit2(word *buf, size_t pos, word op, word imm1, word imm2) {
    buf[pos++] = op;
    buf[pos++] = imm1;
    buf[pos++] = imm2;
    return pos;
}

/* VM main loop: dispatch to backend hooks. OP_PUSH and OP_SET read a type immediate
 * followed by the value immediate.
 */
static inline void run_vm(VM *vm, const Backend *backend) {

    if (backend && backend->setup) backend->setup(vm);

    vm->ip = 0;
    vm->sp = 0;
    vm->tp = 0;
    vm->tp_sp = 0;
    vm->call_sp = 0;
    vm->fp = 0;
    vm->functions_count = 0;
    vm->block_sp = 0;
    memset(vm->tape, 0, sizeof(vm->tape));

    /* initialize types to unknown */
    for (size_t i = 0; i < STACK_SIZE; ++i) vm->types[i] = TYPE_UNKNOWN;
    for (size_t i = 0; i < TAPE_SIZE; ++i) vm->tape_types[i] = TYPE_UNKNOWN;

    while (vm->ip < vm->code_len) {
        OpCode op = (OpCode)vm->code[vm->ip++];

        switch (op) {
            case OP_NOP:
                break;

            case OP_PUSH: {
                /* format: OP_PUSH, type_tag, imm */
                assert(vm->ip + 1 < vm->code_len && "Unexpected end of code (PUSH expects type + imm)");
                int type = (int)vm->code[vm->ip++];
                word imm = vm->code[vm->ip++];
                if (backend && backend->op_push) backend->op_push(vm, type, imm);
                break;
            }

            case OP_ADD:
                if (backend && backend->op_add) backend->op_add(vm);
                break;
            case OP_SUB:
                if (backend && backend->op_sub) backend->op_sub(vm);
                break;
            case OP_MUL:
                if (backend && backend->op_mul) backend->op_mul(vm);
                break;
            case OP_DIV:
                if (backend && backend->op_div) backend->op_div(vm);
                break;
            case OP_REM:
                if (backend && backend->op_rem) backend->op_rem(vm);
                break;

            case OP_MOVE: {
                assert(vm->ip < vm->code_len && "Unexpected end of code (MOVE expects imm)");
                word imm = vm->code[vm->ip++];
                if (backend && backend->op_move) backend->op_move(vm, imm);
                break;
            }

            case OP_LOAD:
                if (backend && backend->op_load) backend->op_load(vm);
                break;
            case OP_STORE:
                if (backend && backend->op_store) backend->op_store(vm);
                break;
            case OP_PRINT:
                if (backend && backend->op_print) backend->op_print(vm);
                break;

            case OP_DEREF:
                if (backend && backend->op_deref) backend->op_deref(vm);
                break;
            case OP_REFER:
                if (backend && backend->op_refer) backend->op_refer(vm);
                break;
            case OP_WHERE:
                if (backend && backend->op_where) backend->op_where(vm);
                break;

            case OP_OFFSET: {
                assert(vm->ip < vm->code_len && "Unexpected end of code (OFFSET expects imm)");
                word imm = vm->code[vm->ip++];
                if (backend && backend->op_offset) backend->op_offset(vm, imm);
                break;
            }

            case OP_INDEX:
                if (backend && backend->op_index) backend->op_index(vm);
                break;

            case OP_SET: {
                /* format: OP_SET, type_tag, imm */
                assert(vm->ip + 1 < vm->code_len && "Unexpected end of code (SET expects type + imm)");
                int type = (int)vm->code[vm->ip++];
                word imm = vm->code[vm->ip++];
                if (backend && backend->op_set) backend->op_set(vm, type, imm);
                break;
            }

            case OP_FUNCTION: {
                assert(vm->ip < vm->code_len && "Unexpected end of code (FUNCTION expects imm)");
                word func_idx = vm->code[vm->ip++];
                if (backend && backend->op_function) backend->op_function(vm, func_idx);
                break;
            }

            case OP_CALL: {
                assert(vm->ip < vm->code_len && "Unexpected end of code (CALL expects imm)");
                word func_idx = vm->code[vm->ip++];
                if (backend && backend->op_call) backend->op_call(vm, func_idx);
                break;
            }

            case OP_RETURN:
                if (backend && backend->op_return) backend->op_return(vm);
                break;

            case OP_WHILE: {
                assert(vm->ip < vm->code_len && "Unexpected end of code (WHILE expects imm)");
                word cond_ip = vm->code[vm->ip++];
                if (backend && backend->op_while) backend->op_while(vm, cond_ip);
                break;
            }

            case OP_IF:
                if (backend && backend->op_if) backend->op_if(vm);
                break;
            case OP_ELSE:
                if (backend && backend->op_else) backend->op_else(vm);
                break;
            case OP_ENDBLOCK:
                if (backend && backend->op_endblock) backend->op_endblock(vm);
                break;

            case OP_ORASSign:
                if (backend && backend->op_orassign) backend->op_orassign(vm);
                break;
            case OP_ANDASSign:
                if (backend && backend->op_andassign) backend->op_andassign(vm);
                break;
            case OP_NOT:
                if (backend && backend->op_not) backend->op_not(vm);
                break;
            case OP_BITAND:
                if (backend && backend->op_bitand) backend->op_bitand(vm);
                break;
            case OP_BITOR:
                if (backend && backend->op_bitor) backend->op_bitor(vm);
                break;
            case OP_BITXOR:
                if (backend && backend->op_bitxor) backend->op_bitxor(vm);
                break;
            case OP_LSH:
                if (backend && backend->op_lsh) backend->op_lsh(vm);
                break;
            case OP_LRSH:
                if (backend && backend->op_lrsh) backend->op_lrsh(vm);
                break;
            case OP_ARSH:
                if (backend && backend->op_arsh) backend->op_arsh(vm);
                break;
            case OP_GEZ:
                if (backend && backend->op_gez) backend->op_gez(vm);
                break;

            case OP_HALT:
                return;

            default:
                fprintf(stderr, "Unknown opcode: %d\n", op);
                exit(1);
        }
    }
}

/* helpers for constructing VM programs in C sources */
#define __init(size) \
    static word prog[size]; \
    size_t p = 0;

#define __fin VM vm = { .code = prog, .code_len = p, .ip = 0, .sp = 0, }; return vm

/* Typed macros are the default. Use the shorthand type constants (e.g. __I64)
 * as the first argument to typed emit macros, e.g. __push(__I64, 1).
 *
 * Backwards-compat: a compatibility alias __push_untyped and __set_untyped
 * are provided if you need the old behaviour (they emit TYPE_UNKNOWN).
 */

/* shorthand type constants (use these when emitting code) */
#define __I8   TYPE_I8
#define __U8   TYPE_U8
#define __I16  TYPE_I16
#define __U16  TYPE_U16
#define __I32  TYPE_I32
#define __U32  TYPE_U32
#define __I64  TYPE_I64
#define __U64  TYPE_U64
#define __F32  TYPE_F32
#define __F64  TYPE_F64
#define __BOOL TYPE_BOOL
#define __PTR  TYPE_PTR
#define __VOID TYPE_VOID
#define __UNKNOWN TYPE_UNKNOWN

/* typed push/set macros: first arg is a type token (one of the __I64/__U32 etc) */
#define __push(typ, imm) p = emit2(prog, p, OP_PUSH, (word)(typ), (word)(imm))
#define __push_t(typ, imm) __push(typ, imm)
#define __set(typ, imm) p = emit2(prog, p, OP_SET, (word)(typ), (word)(imm))
#define __set_t(typ, imm) __set(typ, imm)

/* compatibility aliases that mirror the previous untyped macros (emit TYPE_UNKNOWN) */
#define __push_untyped(imm) p = emit2(prog, p, OP_PUSH, (word)TYPE_UNKNOWN, (word)(imm))
#define __set_untyped(imm) p = emit2(prog, p, OP_SET, (word)TYPE_UNKNOWN, (word)(imm))

#define __add    p = emit0(prog, p, OP_ADD)
#define __sub    p = emit0(prog, p, OP_SUB)
#define __mul    p = emit0(prog, p, OP_MUL)
#define __div    p = emit0(prog, p, OP_DIV)
#define __rem    p = emit0(prog, p, OP_REM)
#define __move(imm) p = emit1(prog, p, OP_MOVE, (word)(imm))
#define __load   p = emit0(prog, p, OP_LOAD)
#define __store  p = emit0(prog, p, OP_STORE)
#define __print  p = emit0(prog, p, OP_PRINT)
#define __halt   p = emit0(prog, p, OP_HALT)

/* pointer/reference emit helpers */
#define __deref  p = emit0(prog, p, OP_DEREF)
#define __refer  p = emit0(prog, p, OP_REFER)
#define __where  p = emit0(prog, p, OP_WHERE)
#define __offset(imm) p = emit1(prog, p, OP_OFFSET, (word)(imm))
#define __index  p = emit0(prog, p, OP_INDEX)

/* control flow emit helpers */
#define __func(idx) p = emit1(prog, p, OP_FUNCTION, (word)(idx))
#define __call(idx) p = emit1(prog, p, OP_CALL, (word)(idx))
#define __ret      p = emit0(prog, p, OP_RETURN)
#define __label(name) size_t __lbl_##name = p
#define __while(name) p = emit1(prog, p, OP_WHILE, (word)__lbl_##name)
#define __if       p = emit0(prog, p, OP_IF)
#define __else     p = emit0(prog, p, OP_ELSE)
#define __end      p = emit0(prog, p, OP_ENDBLOCK)

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

#endif /* VM_H */