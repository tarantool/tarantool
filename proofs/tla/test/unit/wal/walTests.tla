------------------------------- MODULE walTests --------------------------------

EXTENDS Integers, Sequences, TLC, utils
ASSUME LET T == INSTANCE TLC IN T!PrintT("walTests")

CONSTANTS WalMaxRowsTest
ASSUME WalMaxRowsTest \in Nat

VARIABLES tId

--------------------------------------------------------------------------------
\* Imports
--------------------------------------------------------------------------------

CONSTANTS Servers
ASSUME Cardinality(Servers) = 1
VARIABLES wal, box
allVars == <<wal, box>>
INSTANCE wal

--------------------------------------------------------------------------------
\* Unit tests
--------------------------------------------------------------------------------

ASSUME LET entry == XrowEntry(DmlType, "s1", DefaultGroup, SyncFlags, {})
           txn == [id |-> 1, stmts |-> <<entry>>]
           state == [
                rows |-> << >>,
                boxQueue |-> << >>,
                queue |-> <<txn>>
           ]
           expectedEntry == [entry EXCEPT !.lsn = 1]
           expectedTxn == [txn EXCEPT !.stmts = <<expectedEntry>>]
       IN WalProcess(state) = [
            queue |-> << >>,
            rows |-> <<expectedEntry>>,
            boxQueue |-> <<TxMsg(TxWalType, expectedTxn)>>
          ]

--------------------------------------------------------------------------------
\* Specification test
--------------------------------------------------------------------------------

TxnSimulate(i) ==
    /\ tId <= WalMaxRowsTest
    /\ LET entry == XrowEntry(DmlType, i, DefaultGroup, SyncFlags, {})
           txn == [tId |-> tId, stmts |-> <<entry>>]
           walQueue == Append(wal[i].queue, txn)
       IN /\ wal' = VarSet(i, "queue", walQueue, wal)
          /\ tId' = tId + 1
          /\ UNCHANGED <<box>>

Init == /\ WalInit
        /\ box = [i \in Servers |-> [queue |-> <<>>]]
        /\ tId = 1

Next == \/ /\ WalNext(Servers)
           /\ UNCHANGED <<tId>>
        \/ \E i \in Servers: TxnSimulate(i)

Spec == Init /\ [][Next]_allVars /\ WF_allVars(Next)

---------------
\* Properties |
---------------

WalNoGapsAndIncreasingLsnInv ==
    \A i \in Servers:
        LET rows == wal[i].rows
        IN IF Len(rows) > 1 THEN
            \A j \in 2..Len(rows): rows[j].lsn = rows[j-1].lsn + 1
           ELSE TRUE

WalSizeProp ==
    <> \A i \in Servers:
        Len(wal[i].rows) = WalMaxRowsTest

===============================================================================
