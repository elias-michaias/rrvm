#include "../vm/vm.h"

VM __4() {
    __init(192);

    /* Test 4: Calls and pointer interactions
       - function 0: computes 7+10 and returns the value
       - function 1: stores pointer value 4 at tape[0]
       - main: call fn1 to initialize tape[0]=4, call fn0 to compute 17, then store that value into tape[4]
               finally deref (tp = tape[0] -> 4), load tape[4], print
    */

    __func(0);
      /* compute 7 + 10 and return the value */
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
    /* call fn1 first to initialize tape[0] = 4 (tp starts at 0) */
    __call(1);

    /* call fn0 which computes 7+10 and leaves 17 on the stack */
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

