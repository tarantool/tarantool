----------------------------- MODULE limboTests ------------------------------

EXTENDS FiniteSets, definitions, utils

CONSTANTS Servers, ElectionQuorum, SplitBrainCheck, MaxClientRequests
ASSUME Cardinality(Servers) = 1

VARIABLES limbo, limboVclock, limboOwner, limboPromoteGreatestTerm,
          limboPromoteTermMap, limboConfirmedLsn, limboVolatileConfirmedLsn,
          limboConfirmedVclock, limboAckCount, limboSynchroMsg,
          limboPromoteLatch, error, relayLastAck, tId, clientCtr, walQueue,
          state

INSTANCE limbo

allVars == <<limboVars, error, relayLastAck, tId, clientCtr, walQueue, state>>

ASSUME LET T == INSTANCE TLC IN T!PrintT("limboTests")

--------------------------------------------------------------------------------
\* Unit tests
--------------------------------------------------------------------------------

--------------------------------------------------------------------------------
\* Specification test
--------------------------------------------------------------------------------

Init == /\ LimboInit
        /\ clientCtr = 0
        /\ tId = [i \in Servers |-> 0]
        /\ walQueue = [i \in Servers |-> << >>]
        /\ error = [i \in Servers |-> Nil]
        /\ state = [i \in Servers |-> Leader]
        /\ relayLastAck = [i \in Servers |-> [j \in Servers |-> EmptyAck(Servers)]]

Next == LimboNext(Servers)

Spec == Init /\ [][Next]_allVars /\ WF_allVars(Next)

===============================================================================
