% Main optimizer that groups instructions into labeled predicates
% Reads opt/tmp/out.pl and emits opt/tmp/result.pl. It generates clauses lN/0
% for each label N encountered and translates jmp/jz into calls to those
% predicate labels (e.g., jmp(l4), jz(Var, l3)).
%
% This file reads the input using read_term/2 to preserve instruction order
% and avoids consulting the file into this module's database.

:- module(main_opt, [run/0]).
:- use_module(library(lists)).

run :-
    Input = 'opt/tmp/out.pl',
    Output = 'opt/tmp/result.pl',
    (exists_file(Input) -> true ; (format(user_error, 'Input file ~w not found~n', [Input]), fail)),
    read_terms(Input, Terms),

    % Collect consts and build a map Var->Value for rewrites
    findall((V,Val), member(const(V,Val), Terms), ConstPairs),
    dict_create(ConstMap, consts, ConstPairs),

    % Apply simple rewrite: add(D,X,Y) where Y is const 0 => alias(D,X)
    maplist(rewrite_term(ConstMap), Terms, TermsRewritten),

    % Extract consts and aliases to write as top-level facts
    include(is_const, TermsRewritten, ConstsOut),
    include(is_alias, TermsRewritten, AliasesOut),

    % Build labeled blocks from rewritten terms (skip consts/aliases)
    exclude(is_data_fact, TermsRewritten, InstrTerms),
    partition_labels_from_terms(InstrTerms, Blocks),

    % Write result file: consts, aliases, then labeled predicates
    open(Output, write, Out),
    write_list_facts(Out, ConstsOut),
    write_list_facts(Out, AliasesOut),
    write_label_blocks(Out, Blocks),
    close(Out).

% Read all terms from a file into a list (preserve order)
read_terms(Path, Terms) :-
    open(Path, read, In),
    read_terms_stream(In, Terms),
    close(In).

read_terms_stream(In, Terms) :-
    read_term(In, Term, []),
    ( Term == end_of_file -> Terms = [] ; Terms = [Term|Rest], read_terms_stream(In, Rest) ).

% Rewrite a term based on const map: add(D,X,Y) with Y const 0 -> alias(D,X)
rewrite_term(ConstMap, add(D,X,Y), alias(D,X)) :-
    ( get_dict(Y, ConstMap, 0) -> true ; fail ).
rewrite_term(_ConstMap, Term, Term).

% Predicates to classify terms
is_const(const(_, _)).
is_alias(alias(_, _)).
is_data_fact(T) :- is_const(T) ; is_alias(T).

% Partition sequential terms into label blocks. start with label 0 if no leading label
partition_labels_from_terms(Terms, Blocks) :-
    ( Terms = [label(N)|_] -> partition_labels_terms(Terms, [], BlocksRev)
    ; partition_labels_terms([label(0)|Terms], [], BlocksRev)
    ), reverse(BlocksRev, Blocks).

partition_labels_terms([], Curr, [(CurrLabel, CurrInstrs)]) :- CurrLabel = 0, reverse(Curr, CurrInstrs).
partition_labels_terms([label(N)|T], _Acc, [(N,[])|Blocks]) :- partition_labels_terms(T, [], Blocks).
partition_labels_terms([H|T], Acc, Blocks) :-
    ( H = label(N) -> partition_labels_terms(T, [], Blocks)
    ; partition_labels_terms(T, [H|Acc], Blocks)
    ).

% Write top-level facts (list of terms)
write_list_facts(_Out, []) :- !.
write_list_facts(Out, [H|T]) :-
    portray_clause(Out, H),
    write_list_facts(Out, T).

% Write labeled blocks as proper Prolog predicate clauses with indentation
write_label_blocks(Out, Blocks) :-
    forall(member((Label, Instrs), Blocks), (
        format(Out, "~n~a :-\n", [label_head(Label)]),
        write_instrs_body(Out, Instrs),
        format(Out, ".\n", [])
    )).

label_head(Label) :-
    format(atom(H), "l~w", [Label]), H.

write_instrs_body(_Out, []) :- !, format(user_error, "Warning: empty block~n", []).
write_instrs_body(Out, Instrs) :-
    % Translate each instruction into a goal term for the body
    maplist(instr_to_goal, Instrs, Goals),
    % Print goals as comma-separated list with indentation
    write_goals_comma_separated(Out, Goals).

instr_to_goal(jmp(N), jmp(L)) :- format(atom(L), 'l~w', [N]).
instr_to_goal(jz(A,N), jz(A, L)) :- format(atom(L), 'l~w', [N]).
instr_to_goal(print(A), print(A)).
instr_to_goal(call(A), call(A)).
instr_to_goal(call(A,B), call(A,B)).
instr_to_goal(ret, ret).
% Fallback: emit the term itself as a goal (e.g., load/1, store/1)
instr_to_goal(T, T).

write_goals_comma_separated(Out, [G]) :- !, format(Out, "    ~w", [G]).
write_goals_comma_separated(Out, [G|Rest]) :-
    format(Out, "    ~w,~n", [G]),
    write_goals_comma_separated(Out, Rest).

% Helpers
is_data_fact(T) :- is_const(T) ; is_alias(T).


