#include "../vm/vm.h"

VM __5() {
    __init(256);

    /* Test 5: Stress loop that uses pointer stack and nested deref/refer
       - Create a chain: tape[0]=1, tape[1]=2, tape[2]=3
       - deref, deref, deref to move tp along the chain (tp becomes 3)
       - set value 999 at deepest cell
       - refer, refer, refer to restore tp back to 0
       - where and print 0
       - offset(3) then load/print 999
    */

    __set(__PTR, 1); /* tape[0] = 1 */
    __offset(1);
    __set(__PTR, 2); /* tape[1] = 2 */
    __offset(1);
    __set(__PTR, 3); /* tape[2] = 3 */

    /* go back to start */
    __offset(-2);

    /* chase pointers: deref three times */
    __deref; /* tp -> tape[0] = 1 */
    __deref; /* tp -> tape[1] = 2 */
    __deref; /* tp -> tape[2] = 3 */

    __set(__I64, 999);

    /* unwind pointer stack */
    __refer; __refer; __refer;

    __where; __print; /* expect 0 */

    __offset(3);
    __load; __print; /* expect 999 */

    __halt;
    __fin;
}