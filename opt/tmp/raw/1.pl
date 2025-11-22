l0 :-
  const(t0, ptr, 1),
  where(t1),
  set(t1, t0),
  deref(t2, t1),
  const(t3, i64, 123),
  set(t2, t3),
  refer(t4, t2),
  offset(t5, t4, 1),
  load(t6),
  print(t6),
  offset(t7, t5, -1),
  where(t8),
  print(t8).
