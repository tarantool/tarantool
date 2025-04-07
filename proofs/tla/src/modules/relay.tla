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

--------------------------------- MODULE relay ---------------------------------

EXTENDS Integers, Sequences, FiniteSets, utils, definitions

--------------------------------------------------------------------------------
\* Declaration
--------------------------------------------------------------------------------

CONSTANTS Servers
ASSUME Cardinality(Servers) > 0

VARIABLES
    relay, \* Implemented in the current module.
    box,   \* For access of the queue, vclock.
    msgs,  \* See tarantool module.
    wal    \* For access of the rows.

--------------------------------------------------------------------------------
\* Implementation
--------------------------------------------------------------------------------

RelayInit ==
    relay = [i \in Servers |-> [
        relayQueue |-> << >>,
        lastAck |-> [j \in Servers |-> EmptyAck(Servers)],
        sentLsn |-> [j \in Servers |-> 0],
        raftMsg |-> [j \in Servers |-> EmptyGeneralMsg]
    ]]

LOCAL RelayState(i, j) == [
    msgsToSend |-> msgs[i][j][RelaySource],
    msgsToReceive |-> msgs[j][i][ApplierSource],
    relayQueue |-> relay[i].relayQueue,
    lastAck |-> relay[i].lastAck[j],
    sentLsn |-> relay[i].sentLsn[j],
    raftMsg |-> relay[i].raftMsg[j],
    txQueue |-> box[i].queue,
    \* RO variables.
    toInstance |-> j,
    walRows |-> wal[i].rows
]

LOCAL RelayStateApply(i, state) ==
    /\ msgs' = [msgs EXCEPT
        ![i][state.toInstance][RelaySource] = state.msgsToSend,
        ![state.toInstance][i][ApplierSource] = state.msgsToReceive]
    /\ relay' = VarSet(i, "lastAck", [relay[i].lastAck EXCEPT
                            ![state.toInstance] = state.lastAck],
                VarSet(i, "sentLsn", [relay[i].sentLsn EXCEPT
                            ![state.toInstance] = state.sentLsn],
                VarSet(i, "raftMsg", [relay[i].raftMsg EXCEPT
                            ![state.toInstance] = state.raftMsg],
                VarSet(i, "relayQueue", state.relayQueue,
                relay))))
    /\ box' = VarSet(i, "queue", state.txQueue, box)
    /\ UNCHANGED <<wal>>

RelayProcessWalEvent(state) ==
    LET startIdx == FirstEntryWithGreaterLsnIdx(state.walRows, state.sentLsn,
                                                LAMBDA x: x.lsn)
        entries ==
            IF startIdx > 0 THEN
                LET tmp == SubSeq(state.walRows, startIdx, Len(state.walRows))
                IN [j \in 1..Len(tmp) |->
                    IF tmp[j].group_id = LocalGroup THEN [tmp[j] EXCEPT
                        !.type = NopType,
                        !.group_id = DefaultGroup,
                        !.body = << >>
                    ] ELSE tmp[j]]
            ELSE << >>
        newSentLsn == IF entries = << >>
                      THEN state.sentLsn
                      ELSE LastLsn(entries)
    IN [state EXCEPT
        !.msgsToSend = state.msgsToSend \o entries,
        !.sentLsn = newSentLsn
    ]

RelayRaftTrySend(state) ==
    IF state.raftMsg.is_ready = TRUE THEN [state EXCEPT
        !.msgsToSend = Append(state.msgsToSend, state.raftMsg.body),
        !.raftMsg = EmptyGeneralMsg
    ] ELSE state

\* Implementation of the relay_reader_f.
RelayRead(state) ==
    IF Len(state.msgsToReceive) > 0 THEN [state EXCEPT
        !.lastAck = Head(state.msgsToReceive),
        !.msgsToReceive = Tail(state.msgsToReceive)
    ] ELSE state

\* Implementation of the relay_check_status_needs_update.
RelayStatusUpdate(state) ==
    IF Len(state.relayQueue) > 0 THEN [state EXCEPT
        !.txQueue = Append(state.txQueue, TxMsg(TxRelayType, state.lastAck)),
        !.relayQueue = Tail(state.relayQueue)
    ] ELSE state

RelayNext(servers) == \E i, j \in servers:
    LET state == RelayState(i, j)
    IN /\ i # j \* No replication to self.
       /\ \/ RelayStateApply(i, RelayRead(state))
          \/ RelayStateApply(i, RelayStatusUpdate(state))
          \/ RelayStateApply(i, RelayProcessWalEvent(state))
          \/ RelayStateApply(i, RelayRaftTrySend(state))

================================================================================
