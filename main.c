#include "backend/interpreter.h"
#include "vm/vm.h"

int main() {
    word prog[64];
    size_t p = 0;
    
    __push(3);
    __push(4);
    __add();
    __push(5);
    __mul();
    __print();
    __halt();

    VM vm = {
        .code = prog,
        .code_len = p,
        .ip = 0,
        .sp = 0,
    };

    run_vm(&vm, &INTERPRETER_BACKEND);

    return 0;
}
