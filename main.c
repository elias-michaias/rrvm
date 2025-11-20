#define STACK_SIZE 1024
#define WORD_SIZE int64_t
#define BACKEND INTERPRETER_BACKEND
#include "backend/interpreter/interpreter.h"
#include "backend/tac/tac.h"
#include "program.c"

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
