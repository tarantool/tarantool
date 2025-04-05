------------------------------- MODULE walTests --------------------------------

EXTENDS Integers, utils
ASSUME LET T == INSTANCE TLC IN T!PrintT("walTests")

CONSTANTS WalMaxRowsTest
ASSUME WalMaxRowsTest \in Int

--------------------------------------------------------------------------------
\* Imports
--------------------------------------------------------------------------------

CONSTANTS Servers
VARIABLES
    wal,
    walQueue,
    txQueue
allVars == <<wal, walQueue, txQueue>>
INSTANCE wal

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
