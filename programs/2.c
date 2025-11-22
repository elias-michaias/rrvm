#include "../vm/vm.h"

VM __2() {
    __init(256);

    /* function 0: push constants 7 and 35 and return their sum */
    __func(0);
      __push(__I64, 7);
      __push(__I64, 35);
      __add;
      __ret;
    __end;

    /* function 1: push constants 5 and 3 and return their product */
    __func(1);
      __push(__I64, 5);
      __push(__I64, 3);
      __mul;
      __ret;
    __end;

    /* main: call functions, demonstrate if/else and a do-while loop */
    /* call both functions, add their results and print */
    __call(0);
    __call(1);
    __add;
    __print;

    /* if/else demo: condition is true -> prints 100; else would print 200 */
    __push(__I64, 1); /* condition (non-zero = true) */
    __if;
      __push(__I64, 100);
      __print;
    __else;
      __push(__I64, 200);
      __print;
    __end;

    /* while demo (condition-first): check before entering body */
    __push(__I64, 4);   /* initial counter */
    __store;   /* store counter at tape[tp] */

    /* condition: load counter and test */
    __label(cond1);
    __load;
    __while(cond1);
      /* body: load counter, print it, decrement and store back */
      __load;    /* push counter */
      __print;
      __load;    /* push counter */
      __push(__I64, 1);
      __sub;     /* counter - 1 */
      __store;   /* store back */
    __end;

    __halt;

    __fin;
}