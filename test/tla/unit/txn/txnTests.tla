------------------------------- MODULE txnTests --------------------------------

EXTENDS Sequences, FiniteSets, definitions, utils

CONSTANTS Servers, MaxClientRequests
ASSUME Cardinality(Servers) = 1

VARIABLES tId, clientCtr, limbo, limboSynchroMsg, limboPromoteLatch,
          walQueue, state
allVars == <<tId, clientCtr, limbo, limboSynchroMsg, limboPromoteLatch,
             walQueue, state>>

INSTANCE txn

ASSUME LET T == INSTANCE TLC IN T!PrintT("txnTests")

--------------------------------------------------------------------------------
\* Unit tests
--------------------------------------------------------------------------------

--------------------------------------------------------------------------------
\* Specification test
--------------------------------------------------------------------------------

Init == /\ TxnInit
        /\ limbo = [i \in Servers |-> << >>]
        /\ limboSynchroMsg = [i \in Servers |-> EmptyGeneralMsg]
        /\ limboPromoteLatch = [i \in Servers |-> FALSE]
        /\ walQueue = [i \in Servers |-> << >>]
        /\ state = [i \in Servers |-> Leader]

Next == TxnNext(Servers)

Spec == Init /\ [][Next]_allVars /\ WF_allVars(Next)

------------------------------
\* Invariants and properties |
------------------------------

TIdOnlyIncreases ==
    [][\A i \in Servers: tId[i]' >= tId[i]]_tId

LimboSizeEqualsToRequestCtr ==
    \A i \in Servers: clientCtr = Len(limbo[i])

===============================================================================
