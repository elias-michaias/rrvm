#include "../vm/vm.h"

VM __3() {
    __init(128);

    /* Test 3: Mixed pointer arithmetic and indexing (typed)
       - set tape[0] = 2 (pointer to cell 2)
       - deref -> tp becomes tape[0]
       - at tp=2: store 555
       - refer -> restore tp
       - offset(2) -> tp = 2
       - load and print (expect 555)
       - set tape[2] = 1 and then index (tp = tp + tape[tp])
       Note: add explicit __load before __index to produce an index temp for TAC lowering
    */

    /* set tape[0] = 2 (pointer) */
    __set(__PTR, 2);
    /* deref to get tp = tape[0] */
    __deref;
    /* at tp=2: store 555 */
    __set(__I64, 555);
    /* go back */
    __refer;

    __offset(2);
    __load; __print; /* expect 555 */

    /* set tape[2] = 1 and then index (tp = tp + tape[tp]) */
    __offset(-2); /* back to tp=0 */
    __offset(2);  /* tp=2 */
    __set(__I64, 1);
    __load;        /* explicit load: push tape[tp] value (index) */
    __index;      /* tp = tp + tape[tp] => tp = 2 + 1 = 3 */
    __where; __print; /* print 3 */

    __halt;
    __fin;
}
