----------------------------- MODULE boxTests ------------------------------

EXTENDS FiniteSets, definitions, utils

CONSTANTS Servers, SplitBrainCheck, MaxClientRequests, ElectionQuorum,
          MaxRaftTerm

VARIABLES vclock, txQueue, txApplierQueue, error,
          msgs, applierAckMsg, applierVclock, tId, clientCtr,
          walQueue, state, term, volatileTerm, vote, volatileVote, leader,
          votesReceived, leaderWitnessMap, isBroadcastScheduled,
          candidateVclock, relayRaftMsg, limbo, limboVclock, limboOwner,
          limboPromoteGreatestTerm, limboPromoteTermMap, limboConfirmedLsn,
          limboVolatileConfirmedLsn, limboConfirmedVclock, limboAckCount,
          limboSynchroMsg, limboPromoteLatch, relayLastAck
allVars == <<vclock, txQueue, txApplierQueue, error,
             msgs, applierAckMsg, applierVclock, tId, clientCtr, walQueue,
             state, term, volatileTerm, vote, volatileVote, leader,
             votesReceived, leaderWitnessMap, isBroadcastScheduled,
             candidateVclock, limbo, limboVclock, limboOwner,
             limboPromoteGreatestTerm, limboPromoteTermMap, limboConfirmedLsn,
             limboVolatileConfirmedLsn, limboConfirmedVclock, limboAckCount,
             limboSynchroMsg, limboPromoteLatch, relayLastAck>>

INSTANCE box
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

Init == /\ BoxInit
        /\ ApplierInit
        /\ TxnInit
        /\ RaftInit
        /\ LimboInit
        /\ msgs = [i \in Servers |-> [j \in Servers |-> [k \in 1..2 |-> <<>>]]]
        /\ walQueue = [i \in Servers |-> << >>]
        /\ relayLastAck =
                [i \in Servers |-> [j \in Servers |-> EmptyAck(Servers)]]
        /\ relayRaftMsg =
                [i \in Servers |-> [j \in Servers |-> EmptyGeneralMsg]]

Next == BoxNext(Servers)

Spec == Init /\ [][Next]_allVars /\ WF_allVars(Next)

===============================================================================
