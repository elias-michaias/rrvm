#include "vm/vm.h"

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
}
