:- module(main, [run/1]).

:- use_module(library(filesex)).
:- use_module(library(readutil)).
:- use_module(library(apply)).
:- use_module(library(lists)).

%% ------------------------------------------------------------------
%%  Entry point: run(+Index)
%% ------------------------------------------------------------------

run(N) :-
    format("Loading program ~w...~n", [N]),
    raw_file(N, InFile),
    read_program(InFile, Clauses),

    format("Loading optimization passes...~n"),
    load_passes(Passes),

    format("Running pipeline with ~w passes...~n", [Passes]),
    run_pipeline(Passes, Clauses, Optimized),

    format("Writing optimized output...~n"),
    res_file(N, OutFile),
    write_program(OutFile, Optimized),

    format("Done. Output written to ~w~n", [N]).

%% ------------------------------------------------------------------
%%  File paths
%% ------------------------------------------------------------------

raw_file(N, Path) :- format(atom(Path), 'opt/tmp/raw/~w.pl', [N]).
res_file(N, Path) :- format(atom(Path), 'opt/tmp/res/~w.pl', [N]).


%% ------------------------------------------------------------------
%%  Read input program as list of clauses
%% ------------------------------------------------------------------

read_program(File, Clauses) :-
    read_file_to_terms(File, Terms, []),
    include(valid_clause, Terms, Clauses).

valid_clause((Head :- Body)) :-
    atom(Head),
    Body \= true, !.
valid_clause((Head :- true)) :-
    atom(Head), !.

%% ------------------------------------------------------------------
%%  Write output program
%% ------------------------------------------------------------------

write_program(File, Clauses) :-
    open(File, write, Out),
    maplist(write_clause(Out), Clauses),
    close(Out).

write_clause(Out, (Head :- Body)) :-
    format(Out, '~q :-~n', [Head]),
    write_body(Out, Body),
    format(Out, '.~n~n').

write_body(Out, (A, B)) :-
    !, format(Out, '  ~q,~n', [A]),
    write_body(Out, B).
write_body(Out, A) :-
    format(Out, '  ~q~n', [A]).

%% ------------------------------------------------------------------
%%  Load passes from opt/pass/*.pl
%%  Each pass file must define:  pass(+Clauses, -NewClauses).
%% ------------------------------------------------------------------

load_passes(Passes) :-
    expand_file_name('opt/pass/*.pl', Files),
    maplist(ensure_loaded, Files),

    findall(Module:pass,
            ( current_module(Module),
              current_predicate(Module:pass/2)
            ),
            Passes).

%% ------------------------------------------------------------------
%%  Run pipeline: fold passes left-to-right
%% ------------------------------------------------------------------

run_pipeline([], Clauses, Clauses).
run_pipeline([Pass|Rest], Clauses, Out) :-
    call(Pass, Clauses, Mid),
    run_pipeline(Rest, Mid, Out).
