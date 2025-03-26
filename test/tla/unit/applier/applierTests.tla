----------------------------- MODULE applierTests ------------------------------

EXTENDS FiniteSets, definitions, utils

CONSTANTS Servers, SplitBrainCheck, MaxClientRequests, ElectionQuorum,
          MaxRaftTerm

VARIABLES msgs, applierAckMsg, applierVclock, txApplierQueue, tId, clientCtr,
          walQueue, state, term, volatileTerm, vote, volatileVote, leader,
          votesReceived, leaderWitnessMap, isBroadcastScheduled,
          candidateVclock, vclock, relayRaftMsg, limbo, limboVclock, limboOwner,
          limboPromoteGreatestTerm, limboPromoteTermMap, limboConfirmedLsn,
          limboVolatileConfirmedLsn, limboConfirmedVclock, limboAckCount,
          limboSynchroMsg, limboPromoteLatch, error, relayLastAck
allVars == <<msgs, applierAckMsg, applierVclock, tId, clientCtr, walQueue,
             state, term, volatileTerm, vote, volatileVote, leader,
             votesReceived, leaderWitnessMap, isBroadcastScheduled,
             candidateVclock, limbo, limboVclock, limboOwner,
             limboPromoteGreatestTerm, limboPromoteTermMap, limboConfirmedLsn,
             limboVolatileConfirmedLsn, limboConfirmedVclock, limboAckCount,
             limboSynchroMsg, limboPromoteLatch, error, relayLastAck>>

INSTANCE applier
INSTANCE txn
INSTANCE raft
INSTANCE limbo

ASSUME LET T == INSTANCE TLC IN T!PrintT("applierTests")

--------------------------------------------------------------------------------
\* Unit tests
--------------------------------------------------------------------------------

--------------------------------------------------------------------------------
\* Specification test
--------------------------------------------------------------------------------

Init == /\ ApplierInit
        /\ TxnInit
        /\ RaftInit
        /\ LimboInit
        /\ msgs = [i \in Servers |-> [j \in Servers |-> [k \in 1..2 |-> <<>>]]]
        /\ walQueue = [i \in Servers |-> << >>]
        /\ error = [i \in Servers |-> Nil]
        /\ vclock = [i \in Servers |-> [j \in Servers |-> 0]]
        /\ txApplierQueue = [i \in Servers |-> << >>]
        /\ relayLastAck =
                [i \in Servers |-> [j \in Servers |-> EmptyAck(Servers)]]
        /\ relayRaftMsg =
                [i \in Servers |-> [j \in Servers |-> EmptyGeneralMsg]]

Next == ApplierNext(Servers)

Spec == Init /\ [][Next]_allVars /\ WF_allVars(Next)

===============================================================================
