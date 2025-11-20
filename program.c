#include "vm/vm.h"

VM program() {
    __init(128);

    /* function 0: push constants 7 and 35 and return their sum */
    __func(0);
    __push(7);
    __push(35);
    __add();
    __ret();
    __end();

    __func(1);
    __push(5);
    __push(3);
    __mul();
    __ret();
    __end();

    /* main: call function 0 and print result */
    __call(0);
    __call(1);
    __add();
    __print();
    __halt();

    __fin
}
