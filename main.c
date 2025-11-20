#define STACK_SIZE 1024
#define WORD_SIZE int64_t

#include "backend/interpreter/interpreter.h"
#include "backend/tac/tac.h"
#include "vm/vm.h"

#define BACKEND INTERPRETER_BACKEND

VM program() {

    __init(64);

    __push(3);
    __push(4);
    __add();
    __push(5);
    __mul();
    __print();
    __halt();

    __end;

    return vm;
}

int main() {
    VM vm = program();

    run_vm(&vm, &BACKEND);

    if(&BACKEND == &TAC_BACKEND) {
      printf("---- TAC Dump ----\n");
      tac_dump(tac_get_prog(&vm));
    }

    if (BACKEND.finalize) BACKEND.finalize(&vm, 0);
    return 0;
}
