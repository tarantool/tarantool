---------------------------- MODULE tarantoolTests -----------------------------

EXTENDS FiniteSets, definitions, utils

CONSTANTS Servers,
          ElectionQuorum,
          MaxClientRequests,
          SplitBrainCheck,
          MaxRaftTerm,
          MaxHeartbeatsPerTerm

VARIABLES
    msgs,
    error,
    vclock,
    txQueue,
    txApplierQueue,
    tId,
    clientCtr,
    wal,
    walQueue,
    limbo,
    limboVclock,
    limboOwner,
    limboPromoteGreatestTerm,
    limboPromoteTermMap,
    limboConfirmedLsn,
    limboVolatileConfirmedLsn,
    limboConfirmedVclock,
    limboAckCount,
    limboSynchroMsg,
    limboPromoteLatch,
    state,
    term,
    volatileTerm,
    vote,
    volatileVote,
    leader,
    votesReceived,
    leaderWitnessMap,
    isBroadcastScheduled,
    candidateVclock,
    relaySentLsn,
    relayLastAck,
    relayRaftMsg,
    relayHeartbeatCtr,
    applierAckMsg,
    applierVclock

INSTANCE tarantool

-------------------------------------------------------------------------------
\* Properties
-------------------------------------------------------------------------------

\* NOTE: These one is WIP. Ideas for properties are welcome.

\* At most one leader per term.
OneLeaderPerTerm ==
  \A i, j \in AliveServers:
    (i # j /\ state[i] = Leader /\ state[j] = Leader) => term[i] # term[j]

\* All servers are error-free.
NoServerError ==
  \A i \in Servers: error[i] = Nil

TermIsAlwaysOne ==
  \A i \in Servers: term[i] = 1

VolatileTermIsAlwaysOne ==
  \A i \in Servers: volatileTerm[i] = 1

ClientReqIsNotDone ==
  \A i \in Servers: clientCtr = 0

\* DebugCtr == debugCtr < 5

================================================================================
