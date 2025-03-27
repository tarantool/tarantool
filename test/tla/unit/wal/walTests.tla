------------------------------- MODULE walTests --------------------------------

EXTENDS FiniteSets

CONSTANTS Servers
ASSUME Cardinality(Servers) = 1

VARIABLES wal, walQueue, txQueue
allVars == <<wal, walQueue, txQueue>>

INSTANCE wal

ASSUME LET T == INSTANCE TLC IN T!PrintT("walTests")

--------------------------------------------------------------------------------
\* Unit tests
--------------------------------------------------------------------------------

--------------------------------------------------------------------------------
\* Specification test
--------------------------------------------------------------------------------

Init == /\ WalInit
        /\ txQueue = [i \in Servers |-> << >>]

Next == WalNext(Servers)

Spec == Init /\ [][Next]_allVars /\ WF_allVars(Next)

===============================================================================
