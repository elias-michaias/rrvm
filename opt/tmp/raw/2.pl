l1 :-
  const(t0, i64, 7),
  const(t1, i64, 35),
  add(t2, i64, t0, t1),
  ret.

l2 :-
  const(t3, i64, 5),
  const(t4, i64, 3),
  mul(t5, i64, t3, t4),
  ret.

l0 :-
  call(l1, t6),
  call(l2, t7),
  add(t8, unknown, t6, t7),
  print(t8),
  const(t9, i64, 1),
  jz(t9, l3),
  const(t10, i64, 100),
  print(t10),
  jmp(l4).

l3 :-
  const(t11, i64, 200),
  print(t11).

l4 :-
  const(t12, i64, 4),
  store(t12).

l5 :-
  load(t13),
  jz(t13, l6).

l7 :-
  load(t14),
  print(t14),
  load(t15),
  const(t16, i64, 1),
  sub(t17, unknown, t15, t16),
  store(t17),
  jmp(l5).

l6 :-
  true.
