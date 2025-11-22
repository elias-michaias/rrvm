#include "../vm/vm.h"

VM __4() {
    __init(192);

    /* Test 4: Calls and pointer interactions (typed)
       - function 0: computes 7+10 and returns the value (TYPE_I64)
       - function 1: stores pointer value 4 at tape[0] (TYPE_PTR)
       - main: call fn1 to initialize tape[0]=4 (tp starts at 0)
               call fn0 to compute 7+10 (leaves 17 on stack, TYPE_I64),
               then store that value into tape[4] by moving tp temporarily,
               finally deref (tp = tape[0] -> 4), load tape[4], print
    */

    __func(0);
      /* compute 7 + 10 and return the value (signed 64-bit) */
      __push(__I64, 7);
      __push(__I64, 10);
      __add;
      __ret;
    __end;

    __func(1);
      /* set tape[tp] = 4 (a pointer), then ret */
      __set(__PTR, 4);
      __ret;
    __end;

    /* main */
    /* call fn1 first to initialize tape[0] = 4 (tp starts at 0) */
    __call(1);

    /* call fn0 which computes 7+10 and leaves 17 on the stack (TYPE_I64) */
    __call(0);

    /* store the return value into tape[4] by moving tp temporarily */
    __move(4);
    __store;
    __move(-4);

    /* deref -> tp = tape[tp] (should become 4), then load tape[4] and print */
    __deref;
    __offset(0);
    __load;
    __print; /* should print value stored at cell 4 by fn0 */

    __halt;
    __fin;
}