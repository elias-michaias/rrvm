#include "../vm/vm.h"

VM __4() {
    __init(192);

    /* Test 4: Calls and pointer interactions
       - function 0: stores value at tape[0] = arg+10 and returns
       - function 1: stores pointer at tape[1] to cell 4
       - main: call fn0, call fn1, deref, offset, load/print sequence
    */

    __func(0);
          /* simpler: compute 7 + 10 and push the result so the CALL returns it directly */
          __push(7);
          __push(10);
          __add;
      __ret;
    __end;

    __func(1);
      /* set tape[tp] = 4 (a pointer), then ret */
      __set(4);
      __ret;
    __end;

    /* main */
    /* push argument 7 and call fn0 */
    __push(7);
    __call(0);

    /* push nothing and call fn1 (it will set a pointer under tp) */
    __call(1);

    /* deref -> tp = tape[tp] (should become 4) */
    __deref;
    __offset(0);
    __load; __print; /* should print value stored at cell 4 by fn0 */

    __halt;
    __fin;
}

