------------------------------ MODULE VRaftScript ------------------------------
\*
\* The scaffolding of a scripted scenario: a behavior dictated step by step
\* instead of searched for. A script is a plain list of the spec's actions in
\* test/scripted/<name>.tla with its config in the header comment; gen.py
\* makes a module from it extending this one, where Step(i) is the i-th
\* action of the list and the spec runs them in order. TLC checks that every
\* step is enabled in the state the previous ones made, and the invariants on
\* every state - the checkpoints of the script are an invariant too.
\*
\* After the last step the script either stops, and the run ends by the
\* deadlock, or hands over to the spec's own Next, exhaustively to a depth or
\* by random simulation: the script is then the start state of a search over
\* a region the search from Init can't afford to reach.
\*
\* The witness operators are available to the scripts: a script can end by
\* violating one, as a witness config does, or search for one from its end.
\*
EXTENDS VRaftWitness

\* The number of the step to make next: the state after step i has step i + 1.
VARIABLE step

svars == <<vars, step>>

\*
\* Pinning helpers: the spec's actions with their nondeterminism fixed, so
\* that a script line names what happens.
\*

\* The next row of the origin delivered to dst: the one with the given LSN,
\* whichever source offers it.
Deliver(dst, origin, lsn) ==
    /\ Nodes[dst].journal_vclock[origin] = lsn - 1
    /\ NodeReplicateNextEntry(dst)
    /\ Nodes'[dst].journal_vclock[origin] = lsn

\* The node observes exactly this term among the higher ones visible to it.
Observe(nid, term) ==
    /\ NodeObserveHigherTerm(nid)
    /\ Nodes'[nid].election_term = term

\* A checkpoint: a state predicate the script expects to hold after the step
\* above it. gen.py collects them into the Checkpoints invariant; the operator
\* is the predicate itself, so that a script is TLA+ line by line.
Expect(predicate) == predicate

ScriptInit ==
    /\ Init
    /\ step = 1

\* The scripted steps: at step i the i-th action and nothing else. A step not
\* enabled in the state the previous ones made is the script's failure, and is
\* reported as one in every mode - a silent early end would pass a simulation
\* unnoticed.
ScriptNext(len, S(_)) ==
    /\ step <= len
    /\ IF ENABLED S(step)
       THEN /\ step' = step + 1
            /\ S(step)
       ELSE /\ Assert(FALSE, <<"Script step is not enabled", step>>)
            /\ UNCHANGED svars

\* The search after the script: the spec's own Next, with the step counting on
\* so that a config can bound the depth.
ExploreNext(len) ==
    /\ step > len
    /\ step' = step + 1
    /\ Next

================================================================================
