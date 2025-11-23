% rrvm/opt/common.pl
% Shared utility predicates for the optimizer and passes.
% These are expected to be linked into the compiled image (GNU Prolog).
% Predicates are defined in global scope (no modules).

%% ------------------------------------------------------------------
%% Logging helper
%% ------------------------------------------------------------------
log_step(Msg) :-
    catch((
        shell('mkdir -p .tmp'),
        open('.tmp/opt.log', append, S),
        write(S, Msg),
        nl(S),
        close(S)
    ), E,
    ( print_message(error, E), fail )), !.
log_step(_) :- true.

%% ------------------------------------------------------------------
%% Helper: detect missing pass predicates and attempt to load them
%% ------------------------------------------------------------------
%% missing_passes_list(+PassNames, -Missing)
missing_passes_list(PassNames, Missing) :-
    missing_passes_list_acc(PassNames, [], Missing).

missing_passes_list_acc([], Acc, Acc).

missing_passes_list_acc([Name|Rest], Acc, Missing) :-
    ( current_predicate(Name/2) ->
        missing_passes_list_acc(Rest, Acc, Missing)
    ; missing_passes_list_acc(Rest, [Name|Acc], Missing)
    ).

%% attempt_consult_missing(+MissingNames, -Consulted, -StillMissing)
attempt_consult_missing(MissingNames, Consulted, StillMissing) :-
    attempt_consult_missing_acc(MissingNames, [], ConsultedAcc, [], StillAcc),
    reverse(ConsultedAcc, Consulted),
    reverse(StillAcc, StillMissing).

attempt_consult_missing_acc([], Consulted, Consulted, Still, Still).
attempt_consult_missing_acc([Name|Rest], CAcc, Consulted, SAcc, Still) :-
    ( attempt_consult_pass(Name) ->
        attempt_consult_missing_acc(Rest, [Name|CAcc], Consulted, SAcc, Still)
    ;   attempt_consult_missing_acc(Rest, CAcc, Consulted, [Name|SAcc], Still)
    ).

%% attempt_consult_pass(+Name)
attempt_consult_pass(Name) :-
    atom(Name),
    atom_concat('opt/pass/', Name, Prefix),
    atom_concat(Prefix, '.pl', Path),
    format(user_error, "DEBUG: attempting to consult pass file: ~w~n", [Path]),
    ( catch(consult(Path), E,
            ( format(user_error, "DEBUG: consult(~w) raised: ~q~n", [Path, E]), fail )) ->
        ( current_predicate(Name/2) ->
            format(user_error, "DEBUG: consult succeeded and predicate ~w/2 is now present~n", [Name]),
            true
        ;   format(user_error, "DEBUG: consult completed but predicate ~w/2 is still missing~n", [Name]),
            fail
        )
    ;   format(user_error, "DEBUG: consult failed for ~w~n", [Path]),
        fail
    ).

%% ------------------------------------------------------------------
%% Input normalization
%% ------------------------------------------------------------------
normalize_input_path(Input, Path) :-
    ( atom(Input) -> Path = Input
    ; format(user_error, "Error: run/1 expects an atom filename. Got: ~w~n", [Input]), fail
    ).

%% ------------------------------------------------------------------
%% Read input program as list of terms (clauses)
%% ------------------------------------------------------------------
read_program(File, Clauses) :-
    open(File, read, In),
    read_terms(In, Terms),
    close(In),
    filter_valid_terms(Terms, Clauses).

read_terms(Stream, Terms) :-
    read_term(Stream, Term, []),
    ( Term == end_of_file -> Terms = [] ; Terms = [Term|Rest], read_terms(Stream, Rest) ).

filter_valid_terms([], []).
filter_valid_terms([T|Ts], [T|Rs]) :-
    valid_clause(T), !,
    filter_valid_terms(Ts, Rs).
filter_valid_terms([_|Ts], Rs) :-
    filter_valid_terms(Ts, Rs).

valid_clause((Head :- Body)) :-
    atom(Head),
    Body \= true, !.
valid_clause((Head :- true)) :-
    atom(Head), !.
valid_clause(Head) :-
    compound(Head) ; atom(Head).

%% ------------------------------------------------------------------
%% Write output program
%% ------------------------------------------------------------------
write_program(File, Clauses) :-
    open(File, write, Out),
    write_clauses_to_stream(Out, Clauses),
    close(Out).

write_clauses_to_stream(_Out, []) :- !.
write_clauses_to_stream(Out, [C|Cs]) :-
    write_clause_to_stream(Out, C),
    write_clauses_to_stream(Out, Cs).

write_clause_to_stream(Out, (Head :- Body)) :- !,
    write_term(Out, Head, [quoted(true)]),
    write(Out, ' :-'), nl(Out),
    write_body_to_stream(Out, Body),
    write(Out, '.'), nl(Out), nl(Out).
write_clause_to_stream(Out, Head) :-
    write_term(Out, Head, [quoted(true)]),
    write(Out, '.'), nl(Out), nl(Out).

write_body_to_stream(Out, (A,B)) :- !,
    write(Out, '  '), write_term(Out, A, [quoted(true)]), write(Out, ','), nl(Out),
    write_body_to_stream(Out, B).
write_body_to_stream(Out, A) :-
    write(Out, '  '), write_term(Out, A, [quoted(true)]), nl(Out).

%% ------------------------------------------------------------------
%% write_fallback(+InputPathOrResFile, +ResFile) and write_fallback(+ResFile)
%% ------------------------------------------------------------------
write_fallback(InputPath, ResFile) :-
    atom(InputPath),
    atom(ResFile),
    ( catch(open(InputPath, read, S), _E_open, fail) ->
        close(S),
        RawFile = InputPath
    ;  file_base_name(InputPath, Base0),
       ( file_name_extension(Base, _Ext, Base0) -> true ; Base = Base0 ),
       atom_concat('.tmp/raw/', Base, TmpRawPrefix),
       atom_concat(TmpRawPrefix, '.pl', RawFile)
    ),
    format(user_error, "DEBUG: write_fallback attempting to copy from ~w to ~w~n", [RawFile, ResFile]),
    catch(read_program(RawFile, Clauses), E_read,
        ( format(user_error, "write_fallback: failed to read raw file ~w: ~q~n", [RawFile, E_read]), fail )),
    catch(write_program(ResFile, Clauses), E_write,
        ( format(user_error, "write_fallback: failed to write fallback file ~w: ~q~n", [ResFile, E_write]), fail )),
    ( catch(open(ResFile, read, S2), E_open2, ( format(user_error, "write_fallback: open(~w) failed: ~q~n", [ResFile, E_open2]), fail )) ->
        close(S2),
        format(user_error, "write_fallback: succeeded: ~w~n", [ResFile]),
        log_step('write_fallback succeeded:'),
        log_step(ResFile)
    ; format(user_error, "write_fallback: verification open failed for ~w~n", [ResFile]), fail ).

write_fallback(ResFile) :-
    atom(ResFile),
    file_base_name(ResFile, Base0),
    ( file_name_extension(Base, _Ext, Base0) -> true ; Base = Base0 ),
    atom_concat('opt/tmp/raw/', Base, TmpRawPrefix),
    atom_concat(TmpRawPrefix, '.pl', DerivedRaw),
    write_fallback(DerivedRaw, ResFile).

%% ------------------------------------------------------------------
%% Small portable filename helpers
%% ------------------------------------------------------------------
file_base_name(Path, Base) :-
    atom(Path), atom_codes(Path, Codes),
    ( last_slash_pos(Codes, Pos) ->
        Start is Pos + 1,
        length(Codes, Len),
        N is Len - Start,
        sub_codes(Codes, Start, N, BaseCodes),
        atom_codes(Base, BaseCodes)
    ; Base = Path ).

file_name_extension(Name, Ext, File) :-
    atom(File), atom_codes(File, Codes),
    ( last_dot_pos(Codes, Pos) ->
        Start is Pos + 1,
        length(Codes, Len),
        ExtLen is Len - Start,
        sub_codes(Codes, 0, Pos, NameCodes),
        sub_codes(Codes, Start, ExtLen, ExtCodes),
        atom_codes(Name, NameCodes),
        atom_codes(Ext, ExtCodes)
    ; fail ).

last_slash_pos(Codes, Pos) :-
    findall(I, nth0(I, Codes, 0'/), Idxs),
    Idxs \= [],
    last_index(Idxs, Pos).

last_dot_pos(Codes, Pos) :-
    findall(I, nth0(I, Codes, 0'.), Idxs),
    Idxs \= [],
    last_index(Idxs, Pos).

last_index([X], X).
last_index([_|T], X) :- last_index(T, X).

sub_codes(Codes, Start, Len, Sub) :-
    length(Prefix, Start),
    append(Prefix, Rest, Codes),
    length(Sub, Len),
    append(Sub, _, Rest).

%% ------------------------------------------------------------------
%% Portable rrvm_nth0/3
%% ------------------------------------------------------------------
rrvm_nth0(Index, List, Elem) :-
    nonvar(Index), !,
    integer(Index),
    Index >= 0,
    rrvm_nth0_from(Index, List, Elem).
rrvm_nth0(Index, List, Elem) :-
    var(Index),
    rrvm_nth0_enum(0, List, Index, Elem).

rrvm_nth0_from(0, [H|_], H) :- !.
rrvm_nth0_from(N, [_|T], E) :-
    N > 0,
    N1 is N - 1,
    rrvm_nth0_from(N1, T, E).

rrvm_nth0_enum(_, [], _, _) :- fail.
rrvm_nth0_enum(I, [H|T], I, H).
rrvm_nth0_enum(I, [_|T], Index, Elem) :-
    I1 is I + 1,
    rrvm_nth0_enum(I1, T, Index, Elem).

%% ------------------------------------------------------------------
% Optional small helpers useful to passes
% ------------------------------------------------------------------
% term/list membership that avoids accidental unification
member_term(T, [H|_]) :- T == H, !.
member_term(T, [_|T2]) :- member_term(T, T2).

%% rrvm_is_list(+Term)
%% Portable list predicate used instead of relying on `is_list/1`.
rrvm_is_list(X) :- var(X), !, fail.
rrvm_is_list([]).
rrvm_is_list([_|T]) :- rrvm_is_list(T).

%% ------------------------------------------------------------------
%% Pipeline runner with per-pass instrumentation
%% ------------------------------------------------------------------
%% run_pipeline(+Passes, +ClausesIn, -ClausesOut)
%% Delegates to the verbose executor which provides per-pass logs.
run_pipeline([], Clauses, Clauses).
run_pipeline(Passes, Clauses, Out) :-
    execute_passes_verbose(Passes, Clauses, Out).

%% execute_passes_verbose(+PassList, +ClausesIn, -ClausesOut)
%% Run each pass in order, emitting per-pass diagnostics and throwing
%% descriptive exceptions on errors so callers can report root causes.
execute_passes_verbose([], Clauses, Clauses).
execute_passes_verbose([Name|Rest], Clauses, Out) :-
    ( rrvm_is_list(Clauses) -> length(Clauses, InCount) ; InCount = unknown ),
    format(user_error, "EXEC: running pass ~w (input clauses: ~w)~n", [Name, InCount]),
    log_step('exec:pass_start'),
    log_step(Name),
    % Snapshot a small sample for debugging
    log_clause_samples(Name, Clauses, 'input_sample'),

    % Ensure pass predicate exists
    ( current_predicate(Name/2) ->
        true
    ;   format(user_error, "EXEC ERROR: pass predicate ~w/2 missing from image~n", [Name]),
        log_step('exec:pass_missing'),
        log_step(Name),
        throw(error(missing_pass(Name), context(execute_passes_verbose, Name)))
    ),

    % Build goal and call it, capturing exceptions
    Goal =.. [Name, Clauses, Mid],
    ( catch(call(Goal), E,
            ( format(user_error, "EXEC ERROR: pass ~w raised: ~q~n", [Name, E]),
              log_step('exec:pass_exception'),
              log_step(Name),
              log_step(E),
              log_clause_samples(Name, Clauses, E),
              throw(error(pass_raised(Name, E), context(execute_passes_verbose, Name)))
            )) ->
        true
    ;   % call returned false (no output)
        format(user_error, "EXEC ERROR: pass ~w failed (returned false)~n", [Name]),
        log_step('exec:pass_false'),
        log_step(Name),
        log_clause_samples(Name, Clauses, 'returned_false'),
        throw(error(pass_failed(Name), context(execute_passes_verbose, Name)))
    ),

    % Validate result
    ( rrvm_is_list(Mid) ->
        length(Mid, OutCount),
        clauses_diff_stats(Clauses, Mid, Added, Removed),
        format(user_error, "EXEC: pass ~w produced ~w clauses (added: ~w removed: ~w)~n", [Name, OutCount, Added, Removed]),
        log_step('exec:pass_result'),
        log_step(Name),
        log_step(OutCount),
        log_step(Added),
        log_step(Removed),
        execute_passes_verbose(Rest, Mid, Out)
    ;   format(user_error, "EXEC ERROR: pass ~w returned non-list result: ~q~n", [Name, Mid]),
        log_step('exec:pass_nonlist'),
        log_step(Name),
        log_step(Mid),
        log_clause_samples(Name, Clauses, Mid),
        throw(error(pass_bad_result(Name, Mid), context(execute_passes_verbose, Name)))
    ).

%% ------------------------------------------------------------------
%% Debugging helpers: log sample clauses to both stderr and persistent log
%% ------------------------------------------------------------------
%% log_clause_samples(+PassName, +Clauses, +Context)
log_clause_samples(Pass, Clauses, Context) :-
    format(user_error, "DEBUG: clause sample for pass ~w (context: ~q)~n", [Pass, Context]),
    % print a small sample to stderr (up to 5 clauses)
    sample_clauses(Clauses, 5, Sample),
    ( rrvm_is_list(Sample) ->
        print_clause_samples(Sample, user_error)
    ;   write_term(user_error, Sample, [quoted(true)]), nl(user_error)
    ),
    % persist the sample for later analysis
    log_step('clause_sample'),
    log_step(Pass),
    log_step(Context),
    log_step(Sample),
    true.

%% sample_clauses(+Clauses, +N, -Samples)
sample_clauses(Clauses, N, Samples) :-
    ( rrvm_is_list(Clauses) -> take(Clauses, N, Samples) ; Samples = Clauses ).

%% take(+List, +N, -Prefix)
take(_, N, []) :- N =< 0, !.
take([], _, []) :- !.
take([H|T], N, [H|R]) :-
    N1 is N - 1,
    take(T, N1, R).

print_clause_samples(Sample, Stream) :-
    print_clause_samples_impl(Sample, Stream).

print_clause_samples_impl([], _) :- !.
print_clause_samples_impl([C|Cs], Stream) :-
    write_term(Stream, C, [quoted(true)]),
    write(Stream, '.'),
    nl(Stream),
    print_clause_samples_impl(Cs, Stream).

%% ------------------------------------------------------------------
%% Clause diff helpers
%% ------------------------------------------------------------------
clauses_diff_stats(Old, New, Added, Removed) :-
    count_added(Old, New, Added),
    count_removed(Old, New, Removed).

count_added(Old, New, Count) :-
    count_added_impl(Old, New, 0, Count).
count_added_impl(_Old, [], Acc, Acc).
count_added_impl(Old, [H|T], Acc, Count) :-
    ( memberchk(H, Old) -> Acc1 = Acc ; Acc1 is Acc + 1 ),
    count_added_impl(Old, T, Acc1, Count).

count_removed(Old, New, Count) :-
    count_removed_impl(Old, New, 0, Count).
count_removed_impl([], _New, Acc, Acc).
count_removed_impl([H|T], New, Acc, Count) :-
    ( memberchk(H, New) -> Acc1 = Acc ; Acc1 is Acc + 1 ),
    count_removed_impl(T, New, Acc1, Count).

% end of file
