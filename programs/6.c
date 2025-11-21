#include "../vm/vm.h"

VM __6() {
    __init(256);

    /* Demonstrate remainder */
    __push(10);
    __push(3);
    __rem;     /* 10 % 3 = 1 */
    __print;

    /* Logical NOT: not 0 -> 1, not non-zero -> 0 */
    __push(0);
    __not;     /* should become 1 */
    __print;

    /* Bitwise AND */
    __push(5); /* 0b0101 */
    __push(3); /* 0b0011 */
    __bitand;  /* 0b0001 = 1 */
    __print;

    /* Bitwise OR */
    __push(5); /* 0101 */
    __push(2); /* 0010 */
    __bitor;   /* 0111 = 7 */
    __print;

    /* Bitwise XOR */
    __push(6); /* 0110 */
    __push(3); /* 0011 */
    __bitxor;  /* 0101 = 5 */
    __print;

    /* Left shift: 1 << 3 = 8 */
    __push(1);
    __push(3);
    __lsh;
    __print;

    /* Logical right shift: 16 >> 2 = 4 */
    __push(16);
    __push(2);
    __lrsh;
    __print;

    /* Arithmetic right shift: -8 >> 1 = -4 */
    __push(-8);
    __push(1);
    __arsh;
    __print;

    /* GEZ: >= 0 test */
    __push(-1);
    __gez; /* should print 0 */
    __print;
    __push(0);
    __gez; /* should print 1 */
    __print;

    __halt;
    __fin;
}

