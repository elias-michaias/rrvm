#include "../vm/vm.h"

VM __1() {
    __init(64);

    /* Pointer instructions demo (typed):
       - set tape[0] = 1 (a pointer to cell 1)  -> TYPE_PTR
       - deref -> tp becomes tape[0] (1), saving old tp on tp_stack
       - set tape[1] = 123                      -> TYPE_I64
       - refer -> restore tp back to 0
       - offset(1) -> tp = 1
       - load/print -> should print 123
       - offset(-1) -> tp = 0
       - where/print -> print current tp (0)
    */

    /* at tp=0: store pointer to cell 1 */
    __set(__PTR, 1);

    /* dereference: tp -> tape[0] (=1) */
    __deref;

    /* at tp=1: store value 123 (signed 64-bit) */
    __set(__I64, 123);

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