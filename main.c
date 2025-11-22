// VM config
#define STACK_SIZE 1024
#define WORD_BITS 64
#ifndef BACKEND
#define BACKEND __INTERPRETER
#endif


// include backends
#include "backend/interpreter/interpreter.h"
#include "backend/tac/tac.h"

// include program
#include "programs/1.c"
#include "programs/2.c"
#include "programs/3.c"
#include "programs/4.c"
#include "programs/5.c"
#include "programs/6.c"

int main() {
    VM vm[6] = { __1(), __2(), __3(), __4(), __5(), __6() };

    for (int i = 0; i < sizeof(vm)/sizeof(vm[0]); ++i) {
      printf("=== PROGRAM %i ===\n", i+1);

      run_vm(&vm[i], &BACKEND);

      if (&BACKEND == &__TAC) {
        tac_dump(tac_get_prog(&vm[i]));
        char _tac_out_path[64];
        snprintf(_tac_out_path, sizeof(_tac_out_path), "opt/tmp/raw/%d.pl", i+1);
        tac_dump_file(tac_get_prog(&vm[i]), _tac_out_path);
      }

      if (BACKEND.finalize) BACKEND.finalize(&vm[i], 0);
    }

    return 0;
}
