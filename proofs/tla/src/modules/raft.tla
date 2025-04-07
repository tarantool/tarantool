/*
 * Copyright 2010-2025, Tarantool AUTHORS, please see AUTHORS file.
 *
 * Redistribution and use in source and binary forms, with or
 * without modification, are permitted provided that the following
 * conditions are met:
 *
 * 1. Redistributions of source code must retain the above
 *    copyright notice, this list of conditions and the
 *    following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above
 *    copyright notice, this list of conditions and the following
 *    disclaimer in the documentation and/or other materials
 *    provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY <COPYRIGHT HOLDER> ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
 * <COPYRIGHT HOLDER> OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF
 * THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

--------------------------------- MODULE raft ----------------------------------

EXTENDS Integers, Sequences, FiniteSets, Bags

--------------------------------------------------------------------------------
\* Declaration
--------------------------------------------------------------------------------

CONSTANTS
    Servers,        \* A nonempty set of server identifiers.
    ElectionQuorum, \* The number of votes needed to elect a leader.
    MaxRaftTerm     \* The maximum term, which can be achieved in the rs.

ASSUME Cardinality(Servers) > 0
ASSUME /\ ElectionQuorum \in Nat
       /\ ElectionQuorum < Cardinality(Servers)
ASSUME MaxRaftTerm \in Int

\* Raft implementation.
VARIABLES
    state,                \* {"Follower", "Candidate", "Leader"}.
    term,                 \* The current term number of each node.
    volatileTerm,         \* Not yet persisted term.
    vote,                 \* Node vote in its term.
    volatileVote,         \* Not yet persisted vote.
    leader,               \* The leader as known by node.
    votesReceived,        \* The set of nodes, which voted for the candidate.
    isBroadcastScheduled, \* Whether state should be broadcasted to other nodes.
    candidateVclock       \* Vclock of the candidate for which node is voting.

raftVars ==
    <<state, term, volatileTerm, vote, volatileVote, leader, votesReceived,
      isBroadcastScheduled, candidateVclock>>

\* Variables, defined in non imported modules.
VARIABLES
    msgs,         \* tarantool module.
    vclock,       \* box module.
    relayRaftMsg  \* relay module.

--------------------------------------------------------------------------------
\* Imports
--------------------------------------------------------------------------------

\* Limbo substitution.
CONSTANTS SplitBrainCheck, MaxClientRequests

VARIABLES
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
    error,
    relayLastAck,
    tId,
    clientCtr,
    walQueue

LOCAL limboVarsSub ==
    <<limbo, limboVclock, limboOwner, limboPromoteGreatestTerm,
      limboPromoteTermMap, limboConfirmedLsn, limboVolatileConfirmedLsn,
      limboConfirmedVclock, limboAckCount, limboSynchroMsg, limboPromoteLatch,
      error, relayLastAck, tId, clientCtr, walQueue>>

LOCAL INSTANCE limbo
LOCAL INSTANCE definitions
LOCAL INSTANCE utils

--------------------------------------------------------------------------------
\* Implementation
--------------------------------------------------------------------------------

RaftInit ==
    /\ state = [i \in Servers |-> "Follower"]
    /\ term = [i \in Servers |-> 1]
    /\ volatileTerm = [i \in Servers |-> 1]
    /\ vote = [i \in Servers |-> Nil]
    /\ volatileVote = [i \in Servers |-> Nil]
    /\ leader = [i \in Servers |-> Nil]
    /\ votesReceived = [i \in Servers |-> { }]
    /\ isBroadcastScheduled = [i \in Servers |-> FALSE]
    /\ candidateVclock = [i \in Servers |-> [j \in Servers |-> 0]]

\* See RaftProcessMsg, why RaftVars is needed.
RaftVars(i) == [
    state |-> state[i],
    term |-> term[i],
    volatileTerm |-> volatileTerm[i],
    vote |-> vote[i],
    volatileVote |-> volatileVote[i],
    leader |-> leader[i],
    votesReceived |-> votesReceived[i],
    isBroadcastScheduled |-> isBroadcastScheduled[i],
    candidateVclock |-> candidateVclock[i]
]

RaftScheduleBroadcast(i) ==
    isBroadcastScheduled' = [isBroadcastScheduled EXCEPT ![i] = TRUE]

RaftScheduleNewTermVars(vars, newTerm) ==
    [vars EXCEPT
        \* UNCHANGED <<term, vote>>
        !.state = Follower,
        !.volatileTerm = newTerm,
        !.volatileVote = Nil,
        !.leader = Nil,
        !.votesReceived = {},
        !.isBroadcastScheduled = TRUE,
        !.candidateVclock = [i \in Servers |-> 0]
    ]

RaftScheduleNewTerm(i, newTerm) ==
    LET vars == RaftScheduleNewTermVars(RaftVars(i), newTerm)
    IN /\ state' = [state EXCEPT ![i] = vars.state]
       /\ volatileTerm' = [volatileTerm EXCEPT ![i] = vars.volatileTerm]
       /\ volatileVote' = [volatileVote EXCEPT ![i] = vars.volatileVote]
       /\ leader' = [leader EXCEPT ![i] = vars.leader]
       /\ votesReceived' = [votesReceived EXCEPT ![i] = vars.votesReceived]
       /\ isBroadcastScheduled' = [isBroadcastScheduled EXCEPT ![i] =
                                    vars.isBroadcastScheduled]
       /\ candidateVclock' = [candidateVclock EXCEPT ![i] =
                                    vars.candidateVclock]

RaftProcessTerm(i, newTerm) ==
    IF newTerm > volatileTerm[i]
    THEN RaftScheduleNewTerm(i, newTerm)
    ELSE UNCHANGED <<state, volatileTerm, volatileVote, leader, votesReceived,
                     isBroadcastScheduled, candidateVclock>>

RaftScheduleNewVoteVars(vars, j, newCandidateVclock) ==
    [vars EXCEPT
        !.volatileVote = j,
        !.candidateVclock = newCandidateVclock
    ]

RaftScheduleNewVote(i, j, newCandidateVclock) ==
    LET vars == RaftScheduleNewVoteVars(RaftVars(i), j, newCandidateVclock)
    IN /\ volatileVote' = [volatileVote EXCEPT ![i] = vars.volatileVote]
       /\ candidateVclock' = [candidateVclock EXCEPT ![i] =
                                    vars.candidateVclock]

\* Implementation of the raft_can_vote_for.
RaftCanVoteFor(i, newCandidateVclock) ==
    BagCompare(newCandidateVclock, vclock[i]) \in {0, 1}

\* Implementation of the raft_sm_election_update. leader_witness_map is not
\* needed in TLA, since it's impossible to figure out when applier dies.
RaftElectionUpdate(i) ==
    /\ RaftScheduleNewTerm(i, term[i] + 1)
    /\ RaftScheduleNewVote(i, i, vclock[i])

\* Server times out and tries to start new election.
\* Implementation of the raft_sm_election_update_cb.
RaftTimeout(i) ==
    /\ \/ MaxRaftTerm = -1
       \/ SetMax({term[j] : j \in DOMAIN term}) < MaxRaftTerm
    \* In Tarantool timer is stopped on leader.
    /\ IF /\ state[i] \in {Follower, Candidate}
       THEN RaftElectionUpdate(i)
       ELSE UNCHANGED <<state, volatileTerm, volatileVote, leader,
                        votesReceived, isBroadcastScheduled, candidateVclock>>
    /\ UNCHANGED <<msgs, term, vote, limboVarsSub>>

\* Send to WAL if node can vote for the candidate or if only term is changed.
\* Implementation of the raft_worker_io.
RaftWorkerHandleIo(i) ==
    LET xrow == XrowEntry(RaftType, i, LocalGroup, DefaultFlags, [
            term |-> volatileTerm[i],
            vote |-> volatileVote[i]
        ])
        entry == JournalEntry(<<xrow>>, <<>>)
        newWalQueue == Append(walQueue[i], entry)
        voteChanged == volatileVote[i] # vote[i]
        doNotWrite == voteChanged /\ ~RaftCanVoteFor(i, volatileVote[i])
    IN  /\ volatileVote' = IF doNotWrite THEN
                [volatileVote EXCEPT ![i] = Nil] ELSE volatileVote
        /\ candidateVclock' = IF doNotWrite THEN
                [candidateVclock EXCEPT ![i] = EmptyBag] ELSE candidateVclock
        /\ walQueue' = IF ~doNotWrite THEN
                [walQueue EXCEPT ![i] = newWalQueue] ELSE walQueue

RaftBecomeLeaderVars(vars, i) ==
    [vars EXCEPT
        !.state = Leader,
        !.leader = i,
        !.isBroadcastScheduled = TRUE
    ]

RaftBecomeLeader(i) ==
    LET vars == RaftBecomeLeaderVars(RaftVars(i), i)
    IN /\ state' = [state EXCEPT ![i] = vars.state]
       /\ leader' = [leader EXCEPT ![i] = vars.leader]
       /\ isBroadcastScheduled' = [isBroadcastScheduled EXCEPT ![i] =
                                    vars.isBroadcastScheduled]

RaftBecomeCandidate(i) ==
    /\ state' = [state EXCEPT ![i] = Candidate]
    /\ leader' = leader
    /\ RaftScheduleBroadcast(i)

\* Continue implementation of the raft_worker_handle_io.
RaftOnJournalWrite(i, entry) ==
    /\ vote' = [vote EXCEPT ![i] = entry.rows[1].body.vote]
    /\ term' = [term EXCEPT ![i] = entry.rows[1].body.term]
    /\ IF volatileVote[i] = i
       THEN IF ElectionQuorum = 1
            THEN RaftBecomeLeader(i)
            ELSE RaftBecomeCandidate(i)
       ELSE UNCHANGED <<state, leader, isBroadcastScheduled>>

\* Implementation of the raft_worker_handle_broadcast.
RaftWorkerHandleBroadcast(i) ==
    LET xrow == XrowEntry(RaftType, i, DefaultGroup, DefaultFlags, [
            term |-> term[i],
            vote |-> vote[i],
            state |-> state[i],
            leader_id |-> leader[i],
            vclock |-> IF state[i] = Candidate THEN vclock[i] ELSE <<>>
        ])
        newMsgs == [j \in Servers |-> GeneralMsg(xrow)]
    IN relayRaftMsg' = [relayRaftMsg EXCEPT ![i] = newMsgs]

\* Implementation of the box_raft_worker_f.
RaftWorker(i) ==
    /\ \/ /\ \/ volatileTerm[i] # term[i]
             \/ volatileVote[i] # vote[i]
          /\ RaftWorkerHandleIo(i)
          /\ UNCHANGED <<tId, walQueue, error, limbo, limboSynchroMsg,
                         limboVolatileConfirmedLsn, limboPromoteLatch,
                         relayRaftMsg>>
       \/ /\ isBroadcastScheduled[i] = TRUE
          /\ RaftWorkerHandleBroadcast(i)
          /\ UNCHANGED <<walQueue, volatileVote, candidateVclock,
                         tId, error, limbo, limboSynchroMsg,
                         limboVolatileConfirmedLsn, limboPromoteLatch>>
       \/ /\ /\ state[i] = Leader
             /\ limboPromoteTermMap[i][i] # term[i]
          /\ LimboPromoteQsync(i, term[i])
          /\ UNCHANGED <<walQueue, volatileVote, candidateVclock,
                         relayRaftMsg>>
    /\ UNCHANGED <<msgs, limboVars, vclock, state, term, volatileTerm, vote,
                   leader, votesReceived, isBroadcastScheduled>>

RaftApplyVars(i, vars) ==
    /\ state' = [state EXCEPT ![i] = vars.state]
    /\ term' = [term EXCEPT ![i] = vars.term]
    /\ volatileTerm' = [volatileTerm EXCEPT ![i] = vars.volatileTerm]
    /\ vote' = [vote EXCEPT ![i] = vars.vote]
    /\ volatileVote' = [volatileVote EXCEPT ![i] = vars.volatileVote]
    /\ leader' = [leader EXCEPT ![i] = vars.leader]
    /\ votesReceived' = [votesReceived EXCEPT ![i] = vars.votesReceived]
    /\ isBroadcastScheduled' = [isBroadcastScheduled EXCEPT ![i] =
                                    vars.isBroadcastScheduled]
    /\ candidateVclock' = [candidateVclock EXCEPT ![i] = vars.candidateVclock]

\*
\* It's impossible to implement RaftProcessMsg in imperative style, since
\* it's prohibited to update one variable several times and the updated
\* variable is not seen until the end of the step. The function implements
\* the following operator:
\*
RaftProcessMsg(i, entry) ==
    IF entry.body.term >= volatileTerm[i]
    THEN LET raftVarsInit == RaftVars(i)
             raftVarsTerm ==
                IF entry.body.term > raftVarsInit.volatileTerm[i]
                THEN RaftScheduleNewTermVars(raftVars, entry.body.term)
                ELSE raftVarsInit
             raftVarsVote ==
                IF entry.body.vote # 0
                THEN IF raftVarsTerm.state \in {Follower, Leader}
                     THEN IF /\ raftVarsTerm.leader = Nil
                             /\ entry.body.vote # i
                             /\ entry.body.state = Candidate
                             /\ raftVarsTerm.volatileVote = Nil
                             /\ RaftCanVoteFor(i, entry.body.vclock)
                          THEN RaftScheduleNewVoteVars(raftVarsTerm,
                                entry.replica_id, entry.body.vclock)
                          ELSE raftVarsTerm
                     ELSE \* state = Candidate
                          IF entry.body.vote = i
                          THEN LET raftVarsTmp == [raftVarsTerm EXCEPT
                            !.votesReceived = @ \cup {entry.replica_id}]
                               IN IF Cardinality(raftVarsTmp.votesReceived) >=
                                    ElectionQuorum
                                  THEN RaftBecomeLeaderVars(raftVarsTmp, i)
                                  ELSE raftVarsTmp
                          ELSE raftVarsTerm
                ELSE raftVarsTerm
             raftVarsFinal == IF entry.body.state # Leader
                              THEN IF raftVarsVote.leader = entry.replica_id
                                   THEN [raftVarsVote EXCEPT
                                            !.leader = Nil,
                                            !.isBroadcastScheduled = TRUE]
                                   ELSE raftVarsVote
                              ELSE \* entry.body.state = Leader
                                   IF raftVarsVote.leader # entry.replica_id
                                   THEN \* raft_sm_follow_leader
                                        [raftVarsVote EXCEPT
                                            !.state = Follower,
                                            !.leader = entry.replica_id,
                                            !.isBroadcastScheduled = TRUE]
                                   ELSE raftVarsVote
         IN RaftApplyVars(i, raftVarsFinal)
    ELSE UNCHANGED <<raftVars>>

RaftNext(servers) ==
    \/ \E i \in servers: RaftTimeout(i)
    \/ \E i \in servers: RaftWorker(i)

================================================================================
