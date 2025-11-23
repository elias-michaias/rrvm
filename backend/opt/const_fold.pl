% opt/pass/const_fold.pl
% DCG-based constant folding pass for TAC-style Prolog terms.
% GNU Prolog friendly: no module declaration. The pass exposes the public
% interface `const_fold(+Clauses, -NewClauses)` which the driver expects.
%
% This file contains two variants:
%  - conservative DCG-based folding (const_fold_conservative): requires
%    operand temps to appear exactly once.
%  - aggressive DCG-based folding (const_fold_aggressive): relaxes the
%    occurrence restriction and emits debug logs when folding happens.
%
% The public `const_fold/2` currently invokes the aggressive variant and
% logs a small summary; change this if you prefer the conservative pass.

%% Public entry: run aggressive folding and log counts
const_fold(Clauses, NewClauses) :-
    ( rrvm_is_list(Clauses) -> length(Clauses, InCount) ; InCount = unknown ),
    format(user_error, "DEBUG: const_fold (aggressive) start - input clauses: ~w~n", [InCount]),
    const_fold_aggressive_clauses(Clauses, NewClauses),
    ( rrvm_is_list(NewClauses) -> length(NewClauses, OutCount) ; OutCount = unknown ),
    format(user_error, "DEBUG: const_fold (aggressive) end - output clauses: ~w~n", [OutCount]).

%% ------------------------------------------------------------------
%% Conservative folding (retained but not selected by default)
%% ------------------------------------------------------------------
const_fold_conservative(Clauses, NewClauses) :-
    const_fold_clauses(Clauses, NewClauses).

const_fold_clauses([], []).
const_fold_clauses([C|Cs], [NC|NCs]) :-
    fold_clause_dc(C, NC),
    const_fold_clauses(Cs, NCs).

fold_clause_dc((Head :- Body), (Head :- NewBody)) :- !,
    body_to_list(Body, Goals),
    ( phrase(const_fold_pass(Goals, NewGoals), Goals) ->
        ( NewGoals = [] -> NewBody = true ; list_to_body(NewGoals, NewBody) )
    ; NewBody = Body
    ).
fold_clause_dc(Fact, Fact).

%% DCG: conservative pass
const_fold_pass(Orig, Out) --> const_fold_impl(Orig, Out).

const_fold_impl(_Orig, []) --> [].

%% Fold: add
const_fold_impl(Orig, [const(D, Type, V) | Tail]) -->
    [const(A, Type, VA), const(B, Type, VB), add(D, Type, A, B)],
    { integer_type(Type),
      count_occurrences(A, Orig, AC), AC =:= 1,
      count_occurrences(B, Orig, BC), BC =:= 1,
      V is VA + VB
    },
    const_fold_impl(Orig, Tail).

%% Fold: sub
const_fold_impl(Orig, [const(D, Type, V) | Tail]) -->
    [const(A, Type, VA), const(B, Type, VB), sub(D, Type, A, B)],
    { integer_type(Type),
      count_occurrences(A, Orig, AC), AC =:= 1,
      count_occurrences(B, Orig, BC), BC =:= 1,
      V is VA - VB
    },
    const_fold_impl(Orig, Tail).

%% Fold: mul
const_fold_impl(Orig, [const(D, Type, V) | Tail]) -->
    [const(A, Type, VA), const(B, Type, VB), mul(D, Type, A, B)],
    { integer_type(Type),
      count_occurrences(A, Orig, AC), AC =:= 1,
      count_occurrences(B, Orig, BC), BC =:= 1,
      V is VA * VB
    },
    const_fold_impl(Orig, Tail).

%% Fold: div (integer division) - avoid divide by zero
const_fold_impl(Orig, [const(D, Type, V) | Tail]) -->
    [const(A, Type, VA), const(B, Type, VB), div(D, Type, A, B)],
    { integer_type(Type),
      VB =\= 0,
      count_occurrences(A, Orig, AC), AC =:= 1,
      count_occurrences(B, Orig, BC), BC =:= 1,
      V is VA // VB
    },
    const_fold_impl(Orig, Tail).

%% Fold: rem (mod) - avoid divide by zero
const_fold_impl(Orig, [const(D, Type, V) | Tail]) -->
    [const(A, Type, VA), const(B, Type, VB), rem(D, Type, A, B)],
    { integer_type(Type),
      VB =\= 0,
      count_occurrences(A, Orig, AC), AC =:= 1,
      count_occurrences(B, Orig, BC), BC =:= 1,
      V is VA mod VB
    },
    const_fold_impl(Orig, Tail).

%% Default: copy head goal unchanged and continue
const_fold_impl(Orig, [G|Gs]) -->
    [G],
    { true },
    const_fold_impl(Orig, Gs).

%% ------------------------------------------------------------------
%% Aggressive folding (logging + permissive)
%% ------------------------------------------------------------------
const_fold_aggressive_clauses([], []).
const_fold_aggressive_clauses([C|Cs], [NC|NCs]) :-
    fold_clause_aggressive(C, NC),
    const_fold_aggressive_clauses(Cs, NCs).

fold_clause_aggressive((Head :- Body), (Head :- NewBody)) :- !,
    body_to_list(Body, Goals),
    ( phrase(const_fold_aggressive_pass(Goals, NewGoals), Goals) ->
        ( NewGoals = [] -> NewBody = true ; list_to_body(NewGoals, NewBody) )
    ; NewBody = Body
    ).
fold_clause_aggressive(Fact, Fact).

const_fold_aggressive_pass(Orig, Out) --> const_fold_aggressive_impl(Orig, Out).

const_fold_aggressive_impl(_Orig, []) --> [].

%% Aggressive Fold: add
const_fold_aggressive_impl(Orig, [const(D, Type, V) | Tail]) -->
    [const(A, Type, VA), const(B, Type, VB), add(D, Type, A, B)],
    { integer_type(Type),
      V is VA + VB,
      format(user_error, "DEBUG: aggressive fold add -> const(~w,~w,~w) (operands ~w,~w)~n", [D,Type,V,A,B])
    },
    const_fold_aggressive_impl(Orig, Tail).

%% Aggressive Fold: sub
const_fold_aggressive_impl(Orig, [const(D, Type, V) | Tail]) -->
    [const(A, Type, VA), const(B, Type, VB), sub(D, Type, A, B)],
    { integer_type(Type),
      V is VA - VB,
      format(user_error, "DEBUG: aggressive fold sub -> const(~w,~w,~w) (operands ~w,~w)~n", [D,Type,V,A,B])
    },
    const_fold_aggressive_impl(Orig, Tail).

%% Aggressive Fold: mul
const_fold_aggressive_impl(Orig, [const(D, Type, V) | Tail]) -->
    [const(A, Type, VA), const(B, Type, VB), mul(D, Type, A, B)],
    { integer_type(Type),
      V is VA * VB,
      format(user_error, "DEBUG: aggressive fold mul -> const(~w,~w,~w) (operands ~w,~w)~n", [D,Type,V,A,B])
    },
    const_fold_aggressive_impl(Orig, Tail).

%% Aggressive Fold: div (integer division) - avoid divide by zero
const_fold_aggressive_impl(Orig, [const(D, Type, V) | Tail]) -->
    [const(A, Type, VA), const(B, Type, VB), div(D, Type, A, B)],
    { integer_type(Type),
      VB =\= 0,
      V is VA // VB,
      format(user_error, "DEBUG: aggressive fold div -> const(~w,~w,~w) (operands ~w,~w)~n", [D,Type,V,A,B])
    },
    const_fold_aggressive_impl(Orig, Tail).

%% Aggressive Fold: rem (mod) - avoid divide by zero
const_fold_aggressive_impl(Orig, [const(D, Type, V) | Tail]) -->
    [const(A, Type, VA), const(B, Type, VB), rem(D, Type, A, B)],
    { integer_type(Type),
      VB =\= 0,
      V is VA mod VB,
      format(user_error, "DEBUG: aggressive fold rem -> const(~w,~w,~w) (operands ~w,~w)~n", [D,Type,V,A,B])
    },
    const_fold_aggressive_impl(Orig, Tail).

%% Default: copy head goal unchanged and continue
const_fold_aggressive_impl(Orig, [G|Gs]) -->
    [G],
    { true },
    const_fold_aggressive_impl(Orig, Gs).

%% ------------------------------------------------------------------
%% Helpers: body <-> list conversion, occurrence testing and types
%% These are defined once here to avoid discontiguous warnings.
%% ------------------------------------------------------------------
body_to_list(true, []) :- !.
body_to_list((A,B), L) :- !,
    body_to_list(A, LA),
    body_to_list(B, LB),
    append(LA, LB, L).
body_to_list(A, [A]).

list_to_body([], true).
list_to_body([G], G) :- !.
list_to_body([G|Gs], (G, Rest)) :-
    list_to_body(Gs, Rest).

% count_occurrences(+Term, +List, -Count)
% Count exact (term-equal) occurrences of Term in List.
count_occurrences(_, [], 0).
count_occurrences(T, [H|Tails], N) :-
    count_occurrences(T, Tails, N1),
    ( T == H -> N is N1 + 1 ; N is N1 ).

% occurs_in_temp/2 preserved for compatibility with other passes
occurs_in_temp(Temp, Goals) :-
    member(G, Goals),
    term_contains(G, Temp), !.

term_contains(Term, Sub) :-
    Term == Sub, !.
term_contains(Term, Sub) :-
    compound(Term),
    Term =.. [_F|Args],
    term_list_contains(Args, Sub).

term_list_contains([H|_], Sub) :-
    term_contains(H, Sub), !.
term_list_contains([_|T], Sub) :-
    term_list_contains(T, Sub).

% Supported integer-like types (conservative set)
integer_type(i8).
integer_type(u8).
integer_type(i16).
integer_type(u16).
integer_type(i32).
integer_type(u32).
integer_type(i64).
integer_type(u64).
integer_type(bool).

% end of file
