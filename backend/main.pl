% rrvm/opt/main.pl
% Merged command-line driver and optimizer runtime.
% This file replaces the previous split between cmd.pl and main.pl.
% It provides:
%  - cmd_main/0: CLI entry point (initialization)
%  - run/1: optimizer driver that reads a raw TAC file, runs passes,
%            and writes an optimized result to .tmp/res/<basename>.pl.
%  - Static pass list: editors must update `load_passes/1` to name pass
%    predicates (atoms) of arity 2 to be invoked in order.
%
% For interpreted runs you may want to consult pass files before invoking
% the driver. For compiled/native images, ensure pass predicates named in
% `load_passes/1` are linked into the image at build time.
%
% Usage:
%   gplc -o rrvm-opt opt/cmd.pl opt/pass/*.pl
%   ./rrvm-opt .tmp/raw/N.pl
%
% The program attempts to be robust and portable (GNU Prolog friendly).
% No modules are declared; predicates are global.

% optimization passes
load_passes(Modules) :-
    Modules = [const_fold, identity].

:- initialization(cmd_main).

%% ------------------------------------------------------------------
%% CLI entry point
%% ------------------------------------------------------------------
cmd_main :-
    current_prolog_flag(argv, Argv),
    format(user_error, "DEBUG: argv = ~q~n", [Argv]),
    ( Argv = [_Prog, Input|_] -> true
    ; Argv = [Input|_] -> true
    ; print_usage, halt(1)
    ),
    ( atom(Input) -> InputAtom = Input
    ; format(user_error, "Error: expected atom argument, got: ~q~n", [Input]), halt(1)
    ),
    ( catch(open(InputAtom, read, S_pre), E_pre,
            ( format(user_error, "Pre-run: open(~w) failed: ~q~n", [InputAtom, E_pre]), fail ))
    ->  ( format(user_error, "Pre-run: input file ~w is readable~n", [InputAtom]),
          log_step('pre_run:input_readable'),
          log_step(InputAtom),
          close(S_pre)
        )
    ;   ( format(user_error, "Pre-run: input file ~w not readable or missing~n", [InputAtom]),
          log_step('pre_run:input_missing'),
          log_step(InputAtom)
        )
    ),
    % Execute the inline driver inside a guarded catch so we can emit one clear
    % exception on any error and present verbose logging to the user.
    ( catch(
        (   % Normalize input and derive base for result filename
            InputPath = InputAtom,
            format(user_error, "DEBUG: inline driver using input path: ~w~n", [InputPath]),
            file_base_name(InputPath, Base0),
            ( file_name_extension(Base, _Ext, Base0) -> true ; Base = Base0 ),
            atom_concat('.tmp/res/', Base, TmpResPath),
            atom_concat(TmpResPath, '.pl', ResFile),
            format(user_error, "DEBUG: inline driver ResFile = ~w~n", [ResFile]),
            log_step('inline:resolved_resfile'),
            log_step(ResFile),

            % Open and read the raw input file (use the exact provided path)
            ( catch(open(InputPath, read, S), E_open,
                    ( format(user_error, "ERROR: inline open(~w) failed: ~q~n", [InputPath, E_open]),
                      log_step('inline:raw_open_failed'), log_step(InputPath), log_step(E_open),
                      throw(error(raw_open_failed(InputPath), E_open))
                    ))
            ->  close(S)
            ;   throw(error(raw_open_missing(InputPath), context(inline_driver, InputPath)))
            ),

            format(user_error, "Loading program ~w...~n", [InputPath]),
            log_step('inline:before_read_program'),
            ( catch(read_program(InputPath, Clauses), E_read,
                    ( format(user_error, "ERROR: inline read_program(~w) raised: ~q~n", [InputPath, E_read]),
                      log_step('inline:read_program_exception'), log_step(E_read),
                      throw(error(read_program_failed(InputPath), E_read))
                    ))
            ->  true
            ;   throw(error(read_program_failed_no_output(InputPath), context(inline_driver, InputPath)))
            ),
            log_step('inline:after_read_program'),
            ( rrvm_is_list(Clauses) -> length(Clauses, InCount) ; InCount = unknown ),
            format(user_error, "Read ~w clauses from input~n", [InCount]),
            log_step('inline:read_clauses_count'),
            log_step(InCount),

            % Load passes and validate presence
            format(user_error, "Loading optimization passes...~n", []),
            load_passes(Passes),
            format(user_error, "Loaded passes: ~w~n", [Passes]),
            log_step('inline:loaded_passes'),
            log_step(Passes),

            % Verify pass predicates present; attempt consults were handled by load_passes
            ( rrvm_is_list(Passes) ->
                findall(Name, (member(Name, Passes), current_predicate(Name/2)), Present),
                findall(Name, (member(Name, Passes), \+ current_predicate(Name/2)), Missing),
                format(user_error, "DEBUG: pass predicates present: ~w~n", [Present]),
                format(user_error, "DEBUG: pass predicates missing: ~w~n", [Missing]),
                log_step('inline:passes_present'), log_step(Present),
                log_step('inline:passes_missing'), log_step(Missing),
                ( Missing \= [] -> throw(error(missing_passes(Missing), context(inline_driver, Passes))) ; true )
            ; true ),

            % Run pipeline using verbose executor which provides per-pass diagnostics
            format(user_error, "Running pipeline with ~w passes...~n", [Passes]),
            ( catch(execute_passes_verbose(Passes, Clauses, Optimized), E_pipe,
                    ( format(user_error, "ERROR: inline pipeline raised: ~q~n", [E_pipe]),
                      log_step('inline:pipeline_exception'), log_step(E_pipe),
                      throw(E_pipe)
                    ))
            ->  true
            ;   throw(error(pipeline_failed_no_output, context(inline_driver, Passes)))
            ),

            % Write optimized result to ResFile
            ( rrvm_is_list(Optimized) ->
                format(user_error, "Writing optimized output to ~w...~n", [ResFile]),
                shell('mkdir -p .tmp/res'),
                ( catch(write_program(ResFile, Optimized), E_write,
                        ( format(user_error, "ERROR: write_program raised: ~q~n", [E_write]),
                          log_step('inline:write_exception'), log_step(E_write),
                          throw(error(write_program_failed(ResFile), E_write))
                        ))
                ->  ( catch(open(ResFile, read, OutS), E_open2, ( format(user_error, "ERROR: open(~w) after write failed: ~q~n", [ResFile, E_open2]), throw(error(output_verify_failed(ResFile), E_open2)) )) ->
                          close(OutS),
                          format(user_error, "Confirmed write: ~w (wrote ~w clauses)~n", [ResFile, (is_list(Optimized) -> length(Optimized) ; unknown)]),
                          log_step('inline:write_confirmed'),
                          log_step(ResFile)
                      ; throw(error(output_missing_after_write(ResFile), context(inline_driver, ResFile)))
                      )
                )
            ;   throw(error(bad_optimized_result(Optimized), context(inline_driver, ResFile)))
            ),

            % success
            format(user_error, "Done. Output written to ~w~n", [ResFile])
        ),
        Err,
        ( format(user_error, "Exception while running inline optimizer: ~q~n", [Err]),
          log_step('inline:exception'), log_step(Err),
          halt(3)
        )
      )
    ->  halt(0)
    ;   format(user_error, "ERROR: inline driver failed for argument: ~q~n", [InputAtom]),
        format(user_error, "ERROR: Aborting without fallback. Re-run with a valid input or rebuild the optimizer image including passes.~n", []),
        log_step('inline:failed'),
        log_step(InputAtom),
        halt(4)
    ).

print_usage :-
    format(user_error, "RRVM optimizer CLI~nUsage: rrvm-opt <input-path>~nExample: rrvm-opt .tmp/raw/N.pl~n", []).

%% ------------------------------------------------------------------
%% run/1 - the optimizer driver
%% ------------------------------------------------------------------
%% run_and_log(+RawInput)
%% Wrapper around run/1 that logs entry/exit and internal failures so the
%% CLI can provide clearer inline diagnostics when the optimizer is invoked.
run_and_log(RawInput) :-
    format(user_error, "DEBUG: run_and_log: starting for ~w~n", [RawInput]),
    log_step('run_and_log:start'),
    log_step(RawInput),
    ( catch(run(RawInput), E,
            ( format(user_error, "DEBUG: run_and_log: run/1 raised: ~q~n", [E]),
              log_step('run_and_log:exception'),
              log_step(E),
              % Re-throw so the CLI sees the true cause and can report it
              throw(E) )) ->
        format(user_error, "DEBUG: run_and_log: run/1 completed successfully for ~w~n", [RawInput]),
        log_step('run_and_log:success'),
        log_step(RawInput)
    ;   format(user_error, "DEBUG: run_and_log: run/1 returned false for ~w~n", [RawInput]),
        log_step('run_and_log:false'),
        log_step(RawInput),
        % Turn a silent false into an explicit error so callers can handle it.
        throw(error(run_returned_false(RawInput), context(run_and_log, RawInput)))
    ).

%% ------------------------------------------------------------------
%% run/1 - the optimizer driver
%% ------------------------------------------------------------------
run(RawInput) :-
    normalize_input_path(RawInput, InputPath),
    format(user_error, "Using input path: ~w~n", [InputPath]),
    % Persistent diagnostic: mark run start and record the input path in the
    % persistent optimizer log so we can inspect runs from compiled images.
    log_step('run:start'),
    log_step(InputPath),
    % Immediate debug snapshot
    format(user_error, "DEBUG: run entered with RawInput=~w~n", [RawInput]),
    format(user_error, "DEBUG: after normalize_input_path InputPath=~w~n", [InputPath]),
    log_step('debug:after_normalize'),
    log_step(InputPath),

    % derive base name token used for res filename. Use the caller-provided
    % InputPath directly as the RawFile (do not rewrite or prepend paths).
    file_base_name(InputPath, Base0),
    format(user_error, "DEBUG: file_base_name -> Base0=~w~n", [Base0]),
    ( file_name_extension(Base, _Ext, Base0) -> true ; Base = Base0 ),
    format(user_error, "DEBUG: base used for res: ~w~n", [Base]),
    log_step('debug:base_derived'),
    log_step(Base0),

    % Use the unmodified input path as the raw file to read from.
    RawFile = InputPath,
    format(user_error, "DEBUG: RawFile set to InputPath: ~w~n", [RawFile]),
    atom_concat('.tmp/res/', Base, tmp_res_path),
    atom_concat(tmp_res_path, '.pl', ResFile),

    % Debug: resolved filenames
    format(user_error, "Resolved RawFile = ~w~n", [RawFile]),
    format(user_error, "Resolved ResFile = ~w~n", [ResFile]),
    log_step('Resolved RawFile:'),
    log_step(RawFile),
    log_step('Resolved ResFile:'),
    log_step(ResFile),

    % Check raw file existence
    % portable file existence check: attempt to open file for reading (with debug)
    ( catch(open(RawFile, read, S), E, (
            format(user_error, "open(~w) failed: ~q~n", [RawFile, E]),
            % Persist the error and filename for post-mortem inspection.
            log_step('raw_open_failed'),
            log_step(RawFile),
            % If the exception term is printable we record it too.
            ( catch((format(atom(_A, "~q", [E]), true)), _, fail) -> log_step(E) ; true ),
            % Convert missing/unopenable file into an explicit exception so the
            % CLI shows the exact reason rather than silently failing.
            throw(error(raw_open_failed(RawFile), E))
        )) ->
        format(user_error, "Successfully opened ~w for reading~n", [RawFile]),
        % Persist successful open too so we know the exact control flow.
        log_step('raw_open_success'),
        log_step(RawFile),
        close(S)
    ; ( format(user_error, "Error: raw TAC file not found or cannot be opened: ~w~n", [RawFile]),
        log_step('raw_open_missing'),
        log_step(RawFile),
        throw(error(raw_file_not_found(RawFile), context(run/1,RawFile)))
      )
    ),

    format(user_error, "Loading program ~w...~n", [RawFile]),
    log_step('before_read_program'),
    format(user_error, "DEBUG: about to call read_program on ~w~n", [RawFile]),
    ( catch(read_program(RawFile, Clauses), ErrRead,
            ( format(user_error, "ERROR: read_program raised: ~q~n", [ErrRead]),
              log_step('read_program_exception'),
              log_step(ErrRead),
              throw(error(read_program_failed(RawFile), ErrRead))
            ))
    ->  true
    ;   format(user_error, "ERROR: read_program failed for ~w~n", [RawFile]),
        log_step('read_program_failed'),
        log_step(RawFile),
        throw(error(read_program_failed(RawFile), context(run/1, RawFile)))
    ),
    log_step('after_read_program'),
    format(user_error, "DEBUG: read_program returned; verifying Clauses list...~n", []),
    ( rrvm_is_list(Clauses) -> format(user_error, "DEBUG: Clauses is a list of length ~w~n", [length(Clauses)]) ; format(user_error, "DEBUG: Clauses is not a list: ~w~n", [Clauses]) ),
    length(Clauses, InCount),
    format(user_error, "Read ~w clauses from input~n", [InCount]),
    log_step('Read clauses:'),
    log_step(InCount),

    format(user_error, "Loading optimization passes...~n", []),
    log_step('before_load_passes'),
    load_passes(Passes),
    log_step('after_load_passes'),
    format(user_error, "Loaded passes: ~w~n", [Passes]),
    log_step('Loaded passes:'),
    log_step(Passes),

    format(user_error, "Running pipeline with ~w passes...~n", [Passes]),
    log_step('pipeline:about_to_start'),
    log_step(Passes),
    % Record which pass predicate symbols are actually present in this image.
    % This helps diagnose missing/unspecified passes at runtime.
    ( rrvm_is_list(Passes) ->
        findall(Name, (member(Name, Passes), current_predicate(Name/2)), Present),
        findall(Name, (member(Name, Passes), \+ current_predicate(Name/2)), Missing),
        format(user_error, "DEBUG: pass predicates present: ~w~n", [Present]),
        format(user_error, "DEBUG: pass predicates missing: ~w~n", [Missing]),
        log_step('Passes present:'),
        log_step(Present),
        log_step('Passes missing:'),
        log_step(Missing)
    ;   format(user_error, "DEBUG: passes list not available: ~w~n", [Passes]),
        log_step('Passes not a list:'),
        log_step(Passes)
    ),

    % If any pass predicates are missing, attempt to consult their source
    % files from opt/pass/<name>.pl at runtime. Log attempts and final status.
    missing_passes_list(Passes, Missing),
    ( Missing == [] ->
        true
    ;   format(user_error, "DEBUG: missing pass predicates detected: ~w~n", [Missing]),
        log_step('Missing passes:'),
        log_step(Missing),
        % Try to consult candidate files for each missing pass
        attempt_consult_missing(Missing, Consulted, Remaining),
        format(user_error, "DEBUG: attempted consults, consulted: ~w, still missing: ~w~n", [Consulted, Remaining]),
        log_step('Consulted passes:'),
        log_step(Consulted),
        ( Remaining == [] ->
            format(user_error, "DEBUG: all missing passes were successfully consulted and are now available.~n", []),
            log_step('All missing passes consulted')
        ;   format(user_error, "ERROR: some passes remain missing after consult attempts: ~w~n", [Remaining]),
            log_step('Remaining missing passes:'),
            log_step(Remaining),
            % Surface a clear error to abort run if passes are still missing.
            throw(error(missing_passes(Remaining), context(load_passes, Remaining)))
        )
    ),

    format(user_error, "Running pipeline with ~w passes...~n", [Passes]),
    % Persistent diagnostic: mark pipeline start and record passes & input size.
    log_step('pipeline:start'),
    log_step(Passes),
    ( rrvm_is_list(Clauses) -> (catch(length(Clauses, CL0), _, CL0 = unknown), log_step(CL0)) ; true ),
    % Run the pipeline and convert any failure into an explicit exception so
    % the top-level CLI can report precise diagnostics instead of falling back.
    catch((
        ( run_pipeline(Passes, Clauses, Optimized) ->
            true
        ;   % The pipeline returned false (no output); raise an informative error.
            throw(error(pipeline_failed(no_output), context(passes(Passes), clauses(Clauses))))
        )
    ), ErrPipeline, (
        % Pipeline raised an exception; log and re-throw so cmd_main shows it.
        format(user_error, "Pipeline execution raised an exception: ~q~n", [ErrPipeline]),
        % Persist exception details for later inspection
        log_step('pipeline:exception'),
        log_step(ErrPipeline),
        ( catch(length(Clauses, CL), _, CL = unknown) -> format(user_error, "Input clause count: ~w~n", [CL]), log_step(CL) ; true ),
        format(user_error, "Passes attempted: ~w~n", [Passes]),
        log_step('pipeline:passes_attempted'),
        log_step(Passes),
        throw(ErrPipeline)
    )),
    length(Optimized, OutCount),
    format(user_error, "Pipeline produced ~w clauses~n", [OutCount]),
    % Persist pipeline result
    log_step('Pipeline produced:'),
    log_step(OutCount),

    format(user_error, "Writing optimized output to ~w...~n", [ResFile]),
    shell('mkdir -p .tmp/res'),
    ( catch(write_program(ResFile, Optimized), ErrWrite,
            ( format(user_error, "write_program (optimized) raised an exception: ~q~n", [ErrWrite]), fail ))
    ->  ( catch(open(ResFile, read, OutS), ErrOpen, ( format(user_error, "open(~w) after write failed: ~q~n", [ResFile, ErrOpen]), fail )) ->
            ( close(OutS),
              format(user_error, "Confirmed write: ~w (wrote ~w clauses)~n", [ResFile, OutCount]),
              log_step('Wrote optimized file:'),
              log_step(ResFile)
            )
        ; format(user_error, "Error: output file ~w not found after write operation~n", [ResFile]), fail
        )
    ;   % Primary write failed: attempt fallback by writing the original input Clauses
        format(user_error, "Primary write failed; attempting fallback write of original input clauses to ~w~n", [ResFile]),
        ( catch(write_program(ResFile, Clauses), ErrFallback,
                ( format(user_error, "fallback write_program raised an exception: ~q~n", [ErrFallback]), fail ))
        ->  ( catch(open(ResFile, read, OutSF), ErrOpenF, ( format(user_error, "open(~w) after fallback write failed: ~q~n", [ResFile, ErrOpenF]), fail )) ->
                  close(OutSF),
                  format(user_error, "Fallback write succeeded: ~w~n", [ResFile]),
                  log_step('Wrote fallback file:'),
                  log_step(ResFile)
              ; format(user_error, "Error: fallback output file ~w not found after write operation~n", [ResFile]), fail
              )
        ; format(user_error, "Fallback write failed; no output produced for ~w~n", [ResFile]), fail
        )
    ),

    format(user_error, "Done. Output written to ~w~n", [ResFile]).
