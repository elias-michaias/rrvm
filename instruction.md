# Unnamed Virtual Machine

Inspiration: [here](https://github.com/adam-mcdaniel/sage)
Specifically the VM in this section: [here](https://github.com/adam-mcdaniel/sage?tab=readme-ov-file#why-sage)

Text from the inspiration:
> The compiler can target this limited "core" instruction set, with an expanded "standard" instruction set for floating point operations and foreign functions. The core instruction set is designed to be as simple as possible for anyone to implement their own backend. Try to see if you can implement it yourself for your backend of choice!
> The virtual machine has some important optimization properties: Although Sage's VM is a very simple zero-address-code representation, it preserves all the information to reconstruct an LLVM-like three-address-code representation of the original higher level IR. This makes the instruction set capable of applying LLVM's optimizations while being much easier to implement. Sage's innovation is in the backend, not the frontend.
> This combination of simplicity and capacity for optimization was my motivation for creating Sage. I wanted to create a virtual machine with the largest speed + expression + portability to implementation difficulty ratio, and a high level language that could compile to it. I think Sage is a good solution to this problem.

----------------------------

I want to create a virtual machine with very few basic instructions.
The instruction set should be minimal and easy to implement in any backend of choice.
The focus should be on simplicity and portability.
Abstract JUST ENOUGH to get a maxium level of portability in terms of backends.
Something very similar to this.
Help me get started!
For now, we should build with a default backend of x86-64. I'm on Linux.

# Unnamed Virtual Machine

Inspiration: [here](https://github.com/adam-mcdaniel/sage)
Specifically the VM in this section: [here](https://github.com/adam-mcdaniel/sage?tab=readme-ov-file#why-sage)

Text from the inspiration:
> The compiler can target this limited "core" instruction set, with an expanded "standard" instruction set for floating point operations and foreign functions. The core instruction set is designed to be as simple as possible for anyone to implement their own backend. Try to see if you can implement it yourself for your backend of choice!
> The virtual machine has some important optimization properties: Although Sage's VM is a very simple zero-address-code representation, it preserves all the information to reconstruct an LLVM-like three-address-code representation of the original higher level IR. This makes the instruction set capable of applying LLVM's optimizations while being much easier to implement. Sage's innovation is in the backend, not the frontend.
> This combination of simplicity and capacity for optimization was my motivation for creating Sage. I wanted to create a virtual machine with the largest speed + expression + portability to implementation difficulty ratio, and a high level language that could compile to it. I think Sage is a good solution to this problem.

----------------------------

I want to create a virtual machine with very few basic instructions.
The instruction set should be minimal and easy to implement in any backend of choice.
The focus should be on simplicity and portability.
Abstract JUST ENOUGH to get a maxium level of portability in terms of backends.
Something very similar to this.
Help me get started!
For now, we should build with a default backend of x86-64. I'm on Linux.

# Unnamed Virtual Machine

Inspiration: [here](https://github.com/adam-mcdaniel/sage)
Specifically the VM in this section: [here](https://github.com/adam-mcdaniel/sage?tab=readme-ov-file#why-sage)

Text from the inspiration:
> The compiler can target this limited "core" instruction set, with an expanded "standard" instruction set for floating point operations and foreign functions. The core instruction set is designed to be as simple as possible for anyone to implement their own backend. Try to see if you can implement it yourself for your backend of choice!
> The virtual machine has some important optimization properties: Although Sage's VM is a very simple zero-address-code representation, it preserves all the information to reconstruct an LLVM-like three-address-code representation of the original higher level IR. This makes the instruction set capable of applying LLVM's optimizations while being much easier to implement. Sage's innovation is in the backend, not the frontend.
> This combination of simplicity and capacity for optimization was my motivation for creating Sage. I wanted to create a virtual machine with the largest speed + expression + portability to implementation difficulty ratio, and a high level language that could compile to it. I think Sage is a good solution to this problem.

----------------------------

I want to create a virtual machine with very few basic instructions.
The instruction set should be minimal and easy to implement in any backend of choice.
The focus should be on simplicity and portability.
Abstract JUST ENOUGH to get a maxium level of portability in terms of backends.
Something very similar to this.
Help me get started!
For now, we should build with a default backend of x86-64. I'm on Linux.

# Unnamed Virtual Machine

Inspiration: [here](https://github.com/adam-mcdaniel/sage)
Specifically the VM in this section: [here](https://github.com/adam-mcdaniel/sage?tab=readme-ov-file#why-sage)

Text from the inspiration:
> The compiler can target this limited "core" instruction set, with an expanded "standard" instruction set for floating point operations and foreign functions. The core instruction set is designed to be as simple as possible for anyone to implement their own backend. Try to see if you can implement it yourself for your backend of choice!
> The virtual machine has some important optimization properties: Although Sage's VM is a very simple zero-address-code representation, it preserves all the information to reconstruct an LLVM-like three-address-code representation of the original higher level IR. This makes the instruction set capable of applying LLVM's optimizations while being much easier to implement. Sage's innovation is in the backend, not the frontend.
> This combination of simplicity and capacity for optimization was my motivation for creating Sage. I wanted to create a virtual machine with the largest speed + expression + portability to implementation difficulty ratio, and a high level language that could compile to it. I think Sage is a good solution to this problem.

----------------------------

I want to create a virtual machine with very few basic instructions.
The instruction set should be minimal and easy to implement in any backend of choice.
The focus should be on simplicity and portability.
Abstract JUST ENOUGH to get a maxium level of portability in terms of backends.
Something very similar to this.
Help me get started!
For now, we should build with a default backend of x86-64. I'm on Linux.

Here's the list of instructions I want to extend onto the ole' tape machine:


| Instruction | Description |
| ----------- | ----------- |
| Function    | Mark the beginning of a function block |
| Call        | Call the Nth function in the program (N is register value) |
| Return      | Return from the current function |
| While       | Loop block while the register value is non-zero |
| If          | Conditional block if the register value is non-zero |
| Else        | Conditional block if the register value was zero |
| End         | End the current block |

| **Instruction**    | **Description**                                                      |
| ------------------ | -------------------------------------------------------------------- |
| **Add**            | Add the values under the pointer to the register values.             |
| **Sub**            | Subtract the values under the pointer from the register values.      |
| **Mul**            | Multiply the values under the pointer by the register values.        |
| **Div**            | Divide the register values by the values under the pointer.          |
| **Neg**            | Negate the register values.                                          |
| **IsNonNegative?** | Set the register values to 1 if they were non-negative, otherwise 0. |

| **Instruction** | **Description**                                                                   |
| --------------- | --------------------------------------------------------------------------------- |
| **Set**         | Set the registers to some constant values.                                        |
| **Move**        | Move the pointer right or left by a number of steps.                              |
| **Load**        | Load the data under the pointer into the registers.                               |
| **Store**       | Store the data in the registers back to the tape.                                 |
| **Deref**       | Move the pointer to the position specified *under* the pointer (pointer-chasing). |
| **Refer**       | Move the pointer back to the last position before a `Deref`.                      |
| **Where?**      | Store the current pointer position in the registers.                              |
| **Offset**      | Shift the register pointer values by a constant offset.                           |
| **Index**       | Shift register pointer values by the offsets under the pointer.                   |

Instruction	C Equivalent
while	while (reg[0]) {
if	if (reg[0]) {
else	} else {
end	}
set N_0, N_1, ..., N_X	reg[0] = N_0; reg[1] = N_1; ... reg[x] = N_X;
call	funs[reg[0]]();
ret	return;
load N	memcpy(reg, tape_ptr, N * sizeof(cell));
store N	memcpy(tape_ptr, reg, N * sizeof(cell));
move N	tape_ptr += N;
where	reg[0].p = tape_ptr;
deref	push(tape_ptr); tape_ptr = *tape_ptr;
refer	tape_ptr = pop();
index N	for (int i=0; i<N; i++) reg[i].p += tape_ptr->i;
offset O, N	for (int i=0; i<N; i++) reg[i].p += O;
swap N	for (int i=0; i<N; i++) swap(reg + i, tape_ptr + i);
add N	for (int i=0; i<N; i++) reg[i].i += tape_ptr[i].i;
sub N	for (int i=0; i<N; i++) reg[i].i -= tape_ptr[i].i;
mul N	for (int i=0; i<N; i++) reg[i].i *= tape_ptr[i].i;
div N	for (int i=0; i<N; i++) reg[i].i /= tape_ptr[i].i;
rem N	for (int i=0; i<N; i++) reg[i].i %= tape_ptr[i].i;
or N	for (int i=0; i<N; i++) reg[i].i ||= tape_ptr[i].i;
and N	for (int i=0; i<N; i++) reg[i].i &&= tape_ptr[i].i;
not N	for (int i=0; i<N; i++) reg[i].i = !reg[i].i;
bitand N	for (int i=0; i<N; i++) reg[i].i &= tape_ptr[i].i;
bitor N	for (int i=0; i<N; i++) reg[i].i |= tape_ptr[i].i;
bitxor N	for (int i=0; i<N; i++) reg[i].i ^= tape_ptr[i].i;
lsh N	for (int i=0; i<N; i++) reg[i].i <<= tape_ptr[i].i;
l-rsh N	for (int i=0; i<N; i++) reg[i].i = (uint64_t)reg[i].i >> tape_ptr[i].i;
a-rsh N	for (int i=0; i<N; i++) reg[i].i >>= tape_ptr[i].i;
gez N	for (int i=0; i<N; i++) reg[i].i = reg[i].i >= 0;
