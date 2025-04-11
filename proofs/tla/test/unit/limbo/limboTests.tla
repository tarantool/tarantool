----------------------------- MODULE limboTests ------------------------------

EXTENDS FiniteSets, TLC, utils
ASSUME LET T == INSTANCE TLC IN T!PrintT("limboTests")

--------------------------------------------------------------------------------
\* Imports
--------------------------------------------------------------------------------

CONSTANTS Servers, ElectionQuorum, SplitBrainCheck, MaxClientRequests
ASSUME Cardinality(Servers) = 3
ASSUME ElectionQuorum = 2
ASSUME SplitBrainCheck = TRUE
VARIABLES limbo, box, relay, txn, wal, raft
allVars == <<limbo, box, relay, txn, wal, raft>>
INSTANCE limbo

--------------------------------------------------------------------------------
\* Unit tests
--------------------------------------------------------------------------------

LOCAL name == CHOOSE x \in Servers: TRUE
LOCAL fromName == CHOOSE x \in Servers: x # name

LOCAL LimboDefaultState == [
    txns |-> << >>, \* Also passed to txn.
    limboVclock |-> [j \in Servers |-> 0],
    owner |-> name,
    promoteGreatestTerm |-> 0,
    promoteTermMap |-> [j \in Servers |-> 0],
    confirmedLsn |-> 0,
    volatileConfirmedLsn |-> 0,
    confimedVclock |-> [j \in Servers |-> 0],
    ackCount |-> 0,
    synchroMsg |-> EmptyGeneralMsg,
    promoteQsyncLsn |-> 0,
    promoteQsyncTerm |-> 0,
    promoteLatch |-> FALSE,
    \* Txn state.
    tId |-> 0,
    walQueue |-> << >>,
    \* Box state,
    error |-> Nil,
    \* RO variables.
    i |-> name
]

LOCAL defaultVclock == LimboDefaultState.limboVclock
LOCAL TxnBegin(id, entry) == [id |-> id, stmts |-> <<entry>>]

\* Test simple ack.
ASSUME LET lsn == 2
           entry == [XrowEntry(DmlType, name, DefaultGroup, SyncFlags, {})
                     EXCEPT !.lsn = lsn]
           txnToLimbo == TxnBegin(1, entry)
           state == [LimboDefaultState EXCEPT !.txns = <<txnToLimbo>>]
       IN LimboAck(state, name, lsn) = [state EXCEPT
            !.limboVclock = [defaultVclock EXCEPT ![name] = lsn],
            !.ackCount = 1
          ]

\* Test simple confirm after ack, the lsn is assigned before ack.
ASSUME LET lsn == 2
           entry == [XrowEntry(DmlType, name, DefaultGroup, SyncFlags, {})
                     EXCEPT !.lsn = lsn]
           txnToLimbo == TxnBegin(1, entry)
           initState == [LimboDefaultState EXCEPT !.txns = <<txnToLimbo>>]
           state == LimboAck(initState, name, lsn)
       IN LimboAck(state, fromName, lsn) = [state EXCEPT
            !.limboVclock = [defaultVclock EXCEPT
                                ![fromName] = lsn,
                                ![name] = lsn
                            ],
            !.ackCount = 0,
            !.volatileConfirmedLsn = 2
          ]

\* Test ack in the middle of the limbo.
ASSUME LET entry == XrowEntry(DmlType, name, DefaultGroup, SyncFlags, {})
           initState == [LimboDefaultState EXCEPT !.txns = <<
                TxnBegin(1, [entry EXCEPT !.lsn = 1]),
                TxnBegin(2, [entry EXCEPT !.lsn = 2]),
                TxnBegin(3, [entry EXCEPT !.lsn = 3]),
                TxnBegin(4, entry) \* Not yet written.
           >>]
           state == LimboAck(initState, name, 3)
       IN LimboAck(state, fromName, 2) = [state EXCEPT
            !.ackCount = 1,
            !.volatileConfirmedLsn = 2,
            !.limboVclock = [defaultVclock EXCEPT
                ![fromName] = 2,
                ![name] = 3
              ]
          ]

\* Test ack before wal write. Nothing is confirmed.
ASSUME LET entry == XrowEntry(DmlType, name, DefaultGroup, SyncFlags, {})
           state == [LimboDefaultState EXCEPT
                !.txns = <<TxnBegin(1, entry)>>]
       IN LimboAck(state, fromName, 2) = [state EXCEPT
            !.limboVclock = [defaultVclock EXCEPT ![fromName] = 2]
          ]

\* Test ack before wal write and then wal write happens.
ASSUME LET entry == XrowEntry(DmlType, name, DefaultGroup, SyncFlags, {})
           initState == [LimboDefaultState EXCEPT
                !.txns = <<TxnBegin(1, entry)>>]
           state == LimboAck(initState, fromName, 2)
           writtenTxn == TxnBegin(1, [entry EXCEPT !.lsn = 2])
       IN TxnOnJournalWrite(state, writtenTxn) = [state EXCEPT
            !.txns = <<writtenTxn>>,
            !.volatileConfirmedLsn = 2,
            !.limboVclock = [defaultVclock EXCEPT
                ![fromName] = 2,
                ![name] = 2
              ]
          ]

\* Acks gathered, time to write the confirm entry.
ASSUME LET entry == XrowEntry(DmlType, name, DefaultGroup, SyncFlags, {})
           initState == [LimboDefaultState EXCEPT !.txns = <<
                TxnBegin(1, [entry EXCEPT !.lsn = 1]),
                TxnBegin(2, [entry EXCEPT !.lsn = 2]),
                TxnBegin(3, [entry EXCEPT !.lsn = 3]),
                TxnBegin(4, entry) \* Not yet written.
           >>]
           state == LimboAck(LimboAck(initState, fromName, 2), name, 3)
           confirmEntry ==
                XrowEntry(ConfirmType, state.i, DefaultGroup, DefaultFlags, [
                    replica_id |-> state.owner,
                    origin_id |-> state.i,
                    lsn |-> state.volatileConfirmedLsn,
                    term |-> 0
                ])
       IN /\ state.volatileConfirmedLsn # state.confirmedLsn
          /\ PrintT(state)
          /\ PrintT(LimboWriteConfirm(state, state.volatileConfirmedLsn))
          /\ LimboWriteConfirm(state, state.volatileConfirmedLsn) =
                [state EXCEPT
                    !.tId = 1,
                    !.walQueue = <<TxnBegin(1, confirmEntry)>>
                ]

--------------------------------------------------------------------------------
\* Specification test
--------------------------------------------------------------------------------

Init == /\ LimboInit
        /\ box = [i \in Servers |-> [error |-> Nil]]
        /\ relay = [i \in Servers |-> [lastAck |-> EmptyAck(Servers)]]
        /\ txn = [i \in Servers |-> [tId |-> 0, clientCtr |-> 0]]
        /\ wal = [i \in Servers |-> [queue |-> << >>]]
        /\ raft = [i \in Servers |-> [state |-> Leader]]

\* Next == LimboNext(Servers)
Next == UNCHANGED <<allVars>>
Spec == Init /\ [][Next]_allVars /\ WF_allVars(Next)

===============================================================================
