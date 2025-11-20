// VM config
#define STACK_SIZE 1024
#define WORD_BITS 64
#define BACKEND __TAC

// include backends
#include "backend/interpreter/interpreter.h"
#include "backend/tac/tac.h"

// include program
#include "programs/1.c"

int main() {
    VM vm = program();

    run_vm(&vm, &BACKEND);

    if (&BACKEND == &__TAC) {
      printf("---- TAC Dump ----\n");
      tac_dump(tac_get_prog(&vm));
    }

    if (BACKEND.finalize) BACKEND.finalize(&vm, 0);
    return 0;
}
