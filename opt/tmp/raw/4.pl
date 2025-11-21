l1 :-
  const(t0, 7),
  const(t1, 10),
  add(t2, t0, t1),
  ret.

l2 :-
  const(t3, 4),
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
