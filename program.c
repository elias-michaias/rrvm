#include "vm/vm.h"

VM program() {
    // store 7 at tape[0], store 35 at tape[1], then sum them
    __init(64);
  
    // push 7, store -> tape[0]
    __push(7);
    __store();
    // move +1
    __move(1);
    // push 35, store -> tape[1]
    __push(35);
    __store();
    // move -1 (back to 0)
    __move(-1);
    // load tape[0], load tape[1], add, print
    __load();
    __move(1);
    __load();
    __add();
    __print();
    __halt();

    __end;
}
