------------------------------ MODULE raftTests --------------------------------

EXTENDS FiniteSets, utils

CONSTANTS Servers, ElectionQuorum, MaxRaftTerm, SplitBrainCheck,
          MaxClientRequests

ASSUME Cardinality(Servers) = 1

VARIABLES state, term, volatileTerm, vote, volatileVote, leader, votesReceived,
          leaderWitnessMap, isBroadcastScheduled, candidateVclock, limbo,
          limboVclock, limboOwner, limboPromoteGreatestTerm,
          limboPromoteTermMap, limboConfirmedLsn, limboVolatileConfirmedLsn,
          limboConfirmedVclock, limboAckCount, limboSynchroMsg,
          limboPromoteLatch, error, relayLastAck, tId, clientCtr, walQueue,
          msgs, relayRaftMsg, vclock

INSTANCE raft
INSTANCE limbo

allVars == <<raftVars, limboVars, error, relayLastAck, tId, clientCtr,
             walQueue, msgs, relayRaftMsg, vclock>>

ASSUME LET T == INSTANCE TLC IN T!PrintT("raftTests")

--------------------------------------------------------------------------------
\* Unit tests
--------------------------------------------------------------------------------

--------------------------------------------------------------------------------
\* Specification test
--------------------------------------------------------------------------------

Init == /\ RaftInit
        /\ LimboInit
        /\ tId = [i \in Servers |-> 0]
        /\ walQueue = [i \in Servers |-> << >>]
        /\ error = [i \in Servers |-> Nil]
        /\ state = [i \in Servers |-> Leader]
        /\ relayLastAck = [i \in Servers |-> [j \in Servers |-> EmptyAck(Servers)]]
        /\ msgs = [i \in Servers |-> [j \in Servers |-> [k \in 1..2 |-> <<>>]]]
        /\ vclock = [i \in Servers |-> [j \in Servers |-> 0]]
        /\ relayRaftMsg = [i \in Servers |-> [j \in Servers |-> EmptyGeneralMsg]]

Next == RaftNext(Servers)

Spec == Init /\ [][Next]_allVars /\ WF_allVars(Next)

===============================================================================
