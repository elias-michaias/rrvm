:- module(main, [run/1]).

:- use_module(library(filesex)).
:- use_module(library(readutil)).
:- use_module(library(apply)).
:- use_module(library(lists)).

%% ------------------------------------------------------------------
%% RRVM optimization driver (simplified)
%%
%% Policy:
%%  - Every pass file in opt/pass/*.pl MUST declare its own module and
%%    that module MUST export a predicate named `pass/2` whose name is
%%    the same as the file basename. Example:
%%      opt/pass/const_fold.pl  must start with:
%%        :- module(const_fold, [pass/2]).
%%      And the pass is implemented as:
%%        pass(Clauses, NewClauses) :- ...
%%
%%  - The loader enforces this invariant and fails fast with a helpful
%%    error if any pass file does not comply.
%%
%%  - Passes are loaded in filesystem order and executed left-to-right.
%% ------------------------------------------------------------------

%% Entry point: run(+InputPath)
%% InputPath must be an atom or string naming the original source file
%% (e.g. "programs/8.rr" or "8.rr"). The basename (filename without
%% directory and extension) is used to locate the TAC dump at
%% opt/tmp/raw/<basename>.pl and to write the optimized result to
%% opt/tmp/res/<basename>.pl.
run(RawInput) :-
    normalize_input_path(RawInput, InputPath),
    format("Using input path: ~w~n", [InputPath]),

    % Derive basename token used for raw/res filenames
    file_base_name(InputPath, BaseName0),
    ( file_name_extension(BaseName, _Ext, BaseName0) -> true ; BaseName = BaseName0 ),

    format(atom(RawFile), 'opt/tmp/raw/~w.pl', [BaseName]),
    format(atom(ResFile), 'opt/tmp/res/~w.pl', [BaseName]),

    % Ensure raw file exists
    ( exists_file(RawFile) ->
        true
    ; format(user_error, "Error: raw TAC file not found: ~w~n", [RawFile]), fail
    ),

    format("Loading program ~w...~n", [RawFile]),
    read_program(RawFile, Clauses),

    format("Loading optimization passes...~n", []),
    load_passes(Passes),

    format("Running pipeline with ~w passes...~n", [Passes]),
    run_pipeline(Passes, Clauses, Optimized),

    format("Writing optimized output to ~w...~n", [ResFile]),
    make_directory_path('opt/tmp/res'),
    write_program(ResFile, Optimized),

    format("Done. Output written to ~w~n", [ResFile]).

%% ------------------------------------------------------------------
%% Normalize input path (accept atom or string). Reject other types.
%% ------------------------------------------------------------------
normalize_input_path(Input, PathAtom) :-
    ( atom(Input) -> PathAtom = Input
    ; string(Input) -> atom_string(PathAtom, Input)
    ; format(user_error, "Error: run/1 expects a filename (atom or string). Got: ~w~n", [Input]), fail
    ).

%% ------------------------------------------------------------------
%% Read input program as list of clauses
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
%% Write output program (safe stream handling)
%% ------------------------------------------------------------------
write_program(File, Clauses) :-
    setup_call_cleanup(
        open(File, write, Out),
        (   % redirect current output to Out for the writer helpers
            current_output(Prev),
            set_output(Out),
            maplist(write_clause0, Clauses),
            flush_output(Out),
            set_output(Prev)
        ),
        close(Out)
    ).

write_clause0((Head :- Body)) :-
    format('~q :-~n', [Head]),
    write_body0(Body),
    format('.~n~n', []).
write_clause0(Head) :-
    format('~q.~n~n', [Head]).               % support facts (no body)

write_body0((A, B)) :-
    !,
    format('  ~q,~n', [A]),
    write_body0(B).
write_body0(A) :-
    format('  ~q~n', [A]).

%% ------------------------------------------------------------------
%% Load passes from opt/pass/*.pl
%%
%% Enforce: each file must declare a module whose name equals the
%% file basename (without extension) and must export a predicate with
%% the same name and arity 2 (i.e. module M must export M/2).
%% This simplifies the model and avoids any predicate-name collisions.
%% ------------------------------------------------------------------
load_passes(Modules) :-
    expand_file_name('opt/pass/*.pl', Files),
    maplist(load_and_validate_pass, Files, Modules).

%% load_and_validate_pass(+File, -Module)
%% Load the file and ensure it declares module Module and that the
%% module exports a predicate named Module/2.
load_and_validate_pass(File, Module) :-
    % derive expected module name from the file basename
    file_base_name(File, Base),
    ( file_name_extension(BaseName, _Ext, Base) -> Module = BaseName ; Module = Base ),

    % load file (the file must declare its own module directive)
    catch(load_files(File, [silent(true)]), E,
        ( print_message(error, E), format(user_error, "Failed to load pass file: ~w~n", [File]), fail )),

    % ensure the module provides a predicate with the same name and arity 2
    ( current_predicate(Module:Module/2) ->
        true
    ;   format(user_error, "Pass file ~w must declare module ~w and export predicate ~w/2~n", [File, Module, Module]),
        fail
    ).

%% ------------------------------------------------------------------
%% Run pipeline: fold passes left-to-right (list of Module atoms)
%% Each Module must export a predicate Module/2 (predicate name == module).
%% We call the predicate dynamically as Module:Module(Clauses, Mid).
%% ------------------------------------------------------------------
run_pipeline([], Clauses, Clauses).
run_pipeline([Module|Rest], Clauses, Out) :-
    % construct a goal Module(Clauses, Mid) and call it in Module namespace
    PredName = Module,
    Goal =.. [PredName, Clauses, Mid],
    call(Module:Goal),
    run_pipeline(Rest, Mid, Out).
