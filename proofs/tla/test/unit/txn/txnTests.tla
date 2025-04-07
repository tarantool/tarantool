------------------------------- MODULE txnTests --------------------------------

EXTENDS Sequences, FiniteSets, TLC, definitions, utils
ASSUME LET T == INSTANCE TLC IN T!PrintT("txnTests")

--------------------------------------------------------------------------------
\* Imports
--------------------------------------------------------------------------------

CONSTANTS Servers, MaxClientRequests
ASSUME Cardinality(Servers) = 1
VARIABLES txn, wal, limbo, raft
allVars == <<txn, wal, limbo, raft>>
INSTANCE txn

--------------------------------------------------------------------------------
\* Unit tests
--------------------------------------------------------------------------------

LOCAL name == "s1"
LOCAL TxnDefaultState == [
    tId |-> 0,
    clientCtr |-> 0,
    walQueue |-> <<>>,
    txns |-> <<>>,
    \* RO variables.
    raftState |-> Leader,
    promoteLatch |-> FALSE,
    synchroMsg |-> EmptyGeneralMsg
]

\* Basic test.
ASSUME LET entry == XrowEntry(DmlType, name, DefaultGroup, SyncFlags, {})
           txnToWrite == [id |-> 1, stmts |-> <<entry>>]
       IN TxnDo(TxnDefaultState, entry) = [TxnDefaultState EXCEPT
            !.tId = 1,
            !.walQueue = <<txnToWrite>>,
            !.txns = <<txnToWrite>> \* lsn is assigned after write.
          ]

\* Test, that async is not added to limbo.
ASSUME LET entry == XrowEntry(DmlType, name, DefaultGroup, DefaultFlags, {})
           txnToWrite == [id |-> 1, stmts |-> <<entry>>]
       IN TxnDo(TxnDefaultState, entry) = [TxnDefaultState EXCEPT
            !.tId = 1,
            !.walQueue = <<txnToWrite>>
          ]

LOCAL AsyncToLimboFlags == [wait_sync |-> TRUE, wait_ack |-> FALSE]
\* Test, that async is added to limbo, when limbo is non empty.
ASSUME LET entry == XrowEntry(DmlType, name, DefaultGroup, DefaultFlags, {})
           entryToWrite == [entry EXCEPT !.flags = AsyncToLimboFlags]
           txnToWrite == [id |-> 2, stmts |-> <<entryToWrite>>]
           oldTxn == [id |-> 1, stmts |-> <<entry>>]
           state == [TxnDefaultState EXCEPT !.tId = 1, !.txns = <<oldTxn>>]
       IN TxnDo(state, entry) = [state EXCEPT
            !.tId = 2,
            !.walQueue = <<txnToWrite>>,
            !.txns = <<oldTxn, txnToWrite>>
          ]

\* Test, that sync write is not scheduled, if limbo is writing promote.
ASSUME LET entry == XrowEntry(DmlType, name, DefaultGroup, SyncFlags, {})
           state == [TxnDefaultState EXCEPT !.promoteLatch = TRUE]
       IN TxnDo(state, entry) = state

\* Test, that async write is scheduled, even if limbo is writing promote.
ASSUME LET entry == XrowEntry(DmlType, name, DefaultGroup, DefaultFlags, {})
           txnToWrite == [id |-> 1, stmts |-> <<entry>>]
           state == [TxnDefaultState EXCEPT !.promoteLatch = TRUE]
       IN TxnDo(state, entry) = [state EXCEPT
            !.tId = 1,
            !.walQueue = <<txnToWrite>>
          ]

--------------------------------------------------------------------------------
\* Specification test
--------------------------------------------------------------------------------

Init == /\ TxnInit
        /\ wal = [i \in Servers |-> [queue |-> << >>]]
        /\ raft = [i \in Servers |-> [state |-> Leader]]
        /\ limbo = [i \in Servers |-> [
                txns |-> << >>,
                promoteLatch |-> FALSE,
                synchroMsg |-> EmptyGeneralMsg]
           ]

Next == TxnNext(Servers)
Spec == Init /\ [][Next]_allVars /\ WF_allVars(Next)

------------------------------
\* Invariants and properties |
------------------------------

TIdOnlyIncreases ==
    [][\A i \in Servers: txn[i].tId' >= txn[i].tId]_txn

LimboSizeEqualsToRequestCtr ==
    \A i \in Servers: txn[i].clientCtr = Len(limbo[i].txns)

===============================================================================
