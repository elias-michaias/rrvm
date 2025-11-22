:- module(const_fold, [const_fold/2]).

/** <module> const_fold

Simple constant folding pass for TAC-style Prolog terms.

This pass recognizes patterns like:

  const(t0, i32, 7),
  const(t1, i32, 10),
  add(t2, i32, t0, t1)

and replaces them with:

  const(t2, i32, 17)

Only integer-like types are folded (i8,u8,i16,u16,i32,u32,i64,u64,bool).
The pass is conservative: it only folds when the source temps (t0,t1) are
not used later in the clause body (so replacing them by the new const does
not remove later uses unexpectedly).

The pass is intentionally small to serve as a working example.
*/

:- use_module(library(lists)).

%% const_fold(+Clauses, -NewClauses)
const_fold(Clauses, NewClauses) :-
    maplist(fold_clause, Clauses, NewClauses).

%% fold_clause(+Clause, -NewClause)
fold_clause((Head :- Body), (Head :- NewBody)) :- !,
    body_to_list(Body, Goals),
    fold_goals_fixpoint(Goals, NewGoals),
    list_to_body(NewGoals, NewBody).
fold_clause(Fact, Fact).

%% body_to_list(+Body, -Goals)
body_to_list(true, []) :- !.
body_to_list((A,B), L) :- !,
    body_to_list(A, LA),
    body_to_list(B, LB),
    append(LA, LB, L).
body_to_list(A, [A]).

%% list_to_body(+Goals, -Body)
list_to_body([], true).
list_to_body([G], G) :- !.
list_to_body([G|Gs], (G, Rest)) :-
    list_to_body(Gs, Rest).

%% fold_goals_fixpoint(+Goals, -Out)
% Apply folding until a fixpoint is reached.
fold_goals_fixpoint(Goals, Out) :-
    fold_goals_once(Goals, G1),
    ( Goals == G1 -> Out = G1 ; fold_goals_fixpoint(G1, Out) ).

%% fold_goals_once(+Goals, -Out)
% Single left-to-right pass applying fold rules where safe.
fold_goals_once([], []).
fold_goals_once([G|Gs], Out) :-
    ( try_fold([G|Gs], NewPrefix, Remainder) ->
        append(NewPrefix, Tail, Out),
        fold_goals_once(Remainder, Tail)
    ;
        Out = [G|Tail],
        fold_goals_once(Gs, Tail)
    ).

%% try_fold(+Goals, -NewPrefix, -Remainder)
% Match common binary-const patterns and fold them.
% We require the operand temps are not used later in Remainder.

% add
try_fold([const(A, Type, VA), const(B, Type, VB), add(D, Type, A, B)|Rest],
         [const(D, Type, V)], Rest) :-
    integer_type(Type),
    \+ occurs_in_temp(A, Rest),
    \+ occurs_in_temp(B, Rest),
    V is VA + VB.

% sub
try_fold([const(A, Type, VA), const(B, Type, VB), sub(D, Type, A, B)|Rest],
         [const(D, Type, V)], Rest) :-
    integer_type(Type),
    \+ occurs_in_temp(A, Rest),
    \+ occurs_in_temp(B, Rest),
    V is VA - VB.

% mul
try_fold([const(A, Type, VA), const(B, Type, VB), mul(D, Type, A, B)|Rest],
         [const(D, Type, V)], Rest) :-
    integer_type(Type),
    \+ occurs_in_temp(A, Rest),
    \+ occurs_in_temp(B, Rest),
    V is VA * VB.

% div (integer division; avoid divide-by-zero)
try_fold([const(A, Type, VA), const(B, Type, VB), div(D, Type, A, B)|Rest],
         [const(D, Type, V)], Rest) :-
    integer_type(Type),
    VB =\= 0,
    \+ occurs_in_temp(A, Rest),
    \+ occurs_in_temp(B, Rest),
    V is VA // VB.

% rem (mod)
try_fold([const(A, Type, VA), const(B, Type, VB), rem(D, Type, A, B)|Rest],
         [const(D, Type, V)], Rest) :-
    integer_type(Type),
    VB =\= 0,
    \+ occurs_in_temp(A, Rest),
    \+ occurs_in_temp(B, Rest),
    V is VA mod VB.

% bitand
try_fold([const(A, Type, VA), const(B, Type, VB), bitand(D, Type, A, B)|Rest],
         [const(D, Type, V)], Rest) :-
    integer_type(Type),
    \+ occurs_in_temp(A, Rest),
    \+ occurs_in_temp(B, Rest),
    V is VA /\ VB.

% bitor
try_fold([const(A, Type, VA), const(B, Type, VB), bitor(D, Type, A, B)|Rest],
         [const(D, Type, V)], Rest) :-
    integer_type(Type),
    \+ occurs_in_temp(A, Rest),
    \+ occurs_in_temp(B, Rest),
    V is VA \/ VB.

% bitxor
try_fold([const(A, Type, VA), const(B, Type, VB), bitxor(D, Type, A, B)|Rest],
         [const(D, Type, V)], Rest) :-
    integer_type(Type),
    \+ occurs_in_temp(A, Rest),
    \+ occurs_in_temp(B, Rest),
    V is VA xor VB.

%% Helper: occurs_in_temp(+Temp, +Goals)
% True if Temp occurs as a subterm in any goal from Goals.
occurs_in_temp(Temp, Goals) :-
    member(G, Goals),
    sub_term(Temp, G), !.

%% integer_type(+TypeAtom)
integer_type(i8).
integer_type(u8).
integer_type(i16).
integer_type(u16).
integer_type(i32).
integer_type(u32).
integer_type(i64).
integer_type(u64).
integer_type(bool).
