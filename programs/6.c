#include "../vm/vm.h"

VM __6() {
    __init(256);

    /* Demonstrate remainder (signed) */
    __push(__I64, 10);
    __push(__I64, 3);
    __rem;     /* 10 % 3 = 1 */
    __print;

    /* Logical NOT: expect boolean operands */
    __push(__BOOL, 0);
    __not;     /* not 0 -> 1 */
    __print;

    /* Bitwise AND (unsigned) */
    __push(__U64, 5); /* 0b0101 */
    __push(__U64, 3); /* 0b0011 */
    __bitand;  /* 0b0001 = 1 */
    __print;

    /* Bitwise OR (unsigned) */
    __push(__U64, 5); /* 0101 */
    __push(__U64, 2); /* 0010 */
    __bitor;   /* 0111 = 7 */
    __print;

    /* Bitwise XOR (unsigned) */
    __push(__U64, 6); /* 0110 */
    __push(__U64, 3); /* 0011 */
    __bitxor;  /* 0101 = 5 */
    __print;

    /* Left shift: 1 << 3 = 8 (use unsigned) */
    __push(__U64, 1);
    __push(__U64, 3);
    __lsh;
    __print;

    /* Logical right shift: 16 >> 2 = 4 (unsigned) */
    __push(__U64, 16);
    __push(__U64, 2);
    __lrsh;
    __print;

    /* Arithmetic right shift: -8 >> 1 = -4 (signed) */
    __push(__I64, -8);
    __push(__I64, 1);
    __arsh;
    __print;

    /* GEZ: >= 0 test (signed) */
    __push(__I64, -1);
    __gez; /* should print 0 */
    __print;
    __push(__I64, 0);
    __gez; /* should print 1 */
    __print;

    __halt;
    __fin;
}