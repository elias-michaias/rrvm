# RRVM

## What is RRVM?

RRVM (Reconstructing Relational Virtual Machine) is a virtual machine built with a set of minor extensions to a basic tape machine's feature set. It is zero-addressed, meaning that it does not use memory addresses to access data, but instead uses a set of registers to store and manipulate data. This makes it an easy compile target. The advantage of RRVM's instruction set is that it maintains just enough context to be reconstructed into a three-address code (TAC) with single static assignment (SSA), which enables it to be optimized in the same ways that LLVM does.
Both of these attributes keep RRVM as terse as possible - the codebase is small, pleasant, and hackable.

### Reconstructing

In the context of RRVM, "reconstructing" means that the stack machine reconstructs itself into a proper three-address code (TAC). The stack machine does not assign values to temporary variables. There are no assignment statements such as `x = y + z`. Instead, it purely relies on a stack machine with a minimal instruction set that is capable of embedding control flow constructs such as `if` or `func` or `call` - then turns that into a proper TAC format. Consider the following example:

```
func foo
  push i64 7
  push i64 10
  add
  ret
end

func bar
  set ptr 4
  ret
end

# main
call bar
call foo

# store the return value into tape[4] by moving tp temporarily
move 4
store
move -4

# deref -> tp = tape[tp] (should be 4)
deref
offset 0
load
print

halt
```

The `push` instruction pushes a value onto the stack, while the `add` instruction pops two values from the stack and pushes their sum back onto the stack. The `ret` instruction pops the top value from the stack and returns it as the result of the function. Then, a caller can `call` the function to retrieve the result. Here's the output of the program:

```
17
```

One of the weaknesses of a zero-address stack machine intermediate representation (IR) during compilation phases is that it lacks many of the chances to *optimize* that three-address code does. See the following article by *geeksforgeeks* for more information on three-address code: [Three address code in compilers](https://www.geeksforgeeks.org/compiler-design/three-address-code-compiler/).
RRVM addresses this by being able to reconstruct itself into a three-address code format. This means it can simultaneously serve as a convenient compile target (zero-address stack machines have an easier mental model as a compile target) and also have the potential for aggressive optimization (since three-address code is more amenable to optimization techniques).

### Relational

For RRVM, "relational" means that the three-address code format is not expressed in traditional assignment syntax, but is actually generated as a collection of *Prolog terms*. This means that the three-address code is automatically interpreted in Prolog as actual code, and thus, a data structure that can be manipulated trivially. It leverages the power of Prolog's built-in relational programming capabilities to enable efficient and flexible manipulation of the code. Particularly, it leverages Definite Clause Grammars (DCGs) to do term rewriting on arbitrary three-address code. For more information on Definite Clause Grammars (DCGs), see the following article by *Simply Logical*: [Definite Clause Grammars](https://book.simply-logical.space/src/text/3_part_iii/7.2.html). For more information on metaprogramming in Prolog generally and the "code is data" paradigm, see the following article at *The Power of Prolog*: [Prolog Macros](https://www.metalevel.at/prolog/macros). Here's the Prolog three-address code for the above example:

```prolog
l1 :-
  const(t0, i64, 7),
  const(t1, i64, 10),
  add(t2, i64, t0, t1),
  ret.

l2 :-
  const(t3, ptr, 4),
  set(t2, t3),
  ret.

l0 :-
  call(l2, t4),
  call(l1, t5),
  move(4),
  store(t5),
  move(-4),
  deref(t6, t4),
  offset(t7, t6, 0),
  load(t8),
  print(t8).
```

Prolog is able to reason about the relationships between terms and run optimization passes in a very succinct pipeline. For example, the first function can be optimized by the `const_fold.pl` module into:
```prolog
l1 :-
  const(t2, i64, 17), /* 10 + 7 */
  ret.
```

The advantage of using Prolog as an optimization engine is that almost all of our work is done for us by the Prolog engine itself and optimizations can be expressed purely declaratively. This keeps code size down and makes the virtual machine implementation more efficient and pluggable - removing and adding custom optimization passes is as easy as adding new rewrite rules.
