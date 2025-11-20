#include "../vm/vm.h"

VM program() {
    __init(64);

    /* Pointer instructions demo:
       - set tape[0] = 1 (a pointer to cell 1)
       - deref -> tp becomes tape[0] (1), saving old tp on tp_stack
       - set tape[1] = 123
       - refer -> restore tp back to 0
       - offset(1) -> tp = 1
       - load/print -> should print 123
       - offset(-1) -> tp = 0
       - where/print -> print current tp (0)
    */

    /* at tp=0: store pointer to cell 1 */
    __set(1);

    /* dereference: tp -> tape[0] (=1) */
    __deref;

    /* at tp=1: store value 123 */
    __set(123);

    /* go back to previous tp (0) */
    __refer;

    /* move to tp=1 */
    __offset(1);

    /* print tape[1] (should be 123) */
    __load;
    __print;

    /* back to tp=0 and show tp with where */
    __offset(-1);
    __where;
    __print;

    __halt;
    __fin;
}

