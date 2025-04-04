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

EXTENDS Integers, Sequences, FiniteSets

--------------------------------------------------------------------------------
\* Declaration
--------------------------------------------------------------------------------

CONSTANTS Servers

ASSUME Cardinality(Servers) > 0

VARIABLES
    msgs,
    \* Relay implementation.
    relaySentLsn,      \* Last sent LSN to the peer. See relay->r->cursor.
    relayLastAck,      \* Last received ack from replica.
    relayRaftMsg,      \* Raft message for broadcast.
    \* Tx implementation (see box module).
    txQueue,
    vclock,
    \* WAL implementation (see wal module).
    wal,
    \* Raft implementation (see raft module).
    term

relayVars == <<relaySentLsn, relayLastAck, relayRaftMsg>>

--------------------------------------------------------------------------------
\* Imports
--------------------------------------------------------------------------------

LOCAL INSTANCE definitions
LOCAL INSTANCE utils

--------------------------------------------------------------------------------
\* Implementation
--------------------------------------------------------------------------------

RelayInit ==
    /\ relayLastAck = [i \in Servers |-> [j \in Servers |-> EmptyAck(Servers)]]
    /\ relaySentLsn = [i \in Servers |-> [j \in Servers |-> 0]]
    /\ relayRaftMsg = [i \in Servers |-> [j \in Servers |-> EmptyGeneralMsg]]

\* Implementation of the relay_process_wal_event.
RelayProcessWalEvent(i, j) ==
    LET startIdx == FirstEntryMoreLsnIdx(wal[i], relaySentLsn[i][j],
                                         LAMBDA x: x.lsn)
        entries == IF startIdx > 0
                   THEN SubSeq(wal[i], startIdx, Len(wal[i]))
                   ELSE << >>
        globalEntries == SelectSeq(entries, XrowEntryIsGlobal)
        newSentLsn == IF globalEntries = <<>>
                      THEN relaySentLsn[i][j]
                      ELSE LastLsn(globalEntries)
    IN /\ msgs' = [msgs EXCEPT ![i][j] = msgs[i][j] \o entries]
       /\ relaySentLsn' = [relaySentLsn EXCEPT ![i][j] = newSentLsn]
       /\ UNCHANGED
            \* Without {msgs, relaySentLsn}.
            <<relayLastAck, relayRaftMsg, txQueue>>

RelayRaftSend(i, j) ==
    /\ Send(msgs, i, j, RelaySource, relayRaftMsg.body)
    /\ LET newMsg == [relayRaftMsg[i][j] EXCEPT !.is_ready = FALSE]
       IN relayRaftMsg = [relayRaftMsg EXCEPT ![i][j] = newMsg]
    /\ UNCHANGED
            \* Without {msgs, relayRaftMsg}.
            <<relayLastAck, relaySentLsn, txQueue>>

\* Implementation of the relay_reader_f.
RelayRead(i, j) ==
    /\ Len(msgs[j][i][ApplierSource]) > 0
    /\ relayLastAck' = [relayLastAck EXCEPT ![i][j] =
            Head(msgs[j][i][ApplierSource])]
    /\ msgs' = [msgs EXCEPT ![j][i][ApplierSource] =
            Tail(msgs[j][i][ApplierSource])]
    /\ UNCHANGED
            \* Without {msgs, relayLastAck}.
            <<relaySentLsn, relayRaftMsg, txQueue>>

\* Implementation of the relay_check_status_needs_update.
RelayStatusUpdate(i, j) ==
    LET newTxQueue == Append(txQueue[i], TxMsg(TxRelayType, relayLastAck[i][j]))
    IN /\ relayLastAck[i][j] # EmptyAck(Servers)
       /\ txQueue' = [txQueue EXCEPT ![i] = newTxQueue]
       /\ relayLastAck' = [relayLastAck EXCEPT ![i][j] = EmptyAck(Servers)]
       /\ UNCHANGED
            \* Without {txQueue, relayLastAck}
            <<msgs, relaySentLsn, relayRaftMsg>>

RelayProcess(i, j) ==
    /\ IF i # j \* No replication to self.
       THEN \/ RelayRead(i, j)
            \/ RelayStatusUpdate(i, j)
            \/ RelayProcessWalEvent(i, j)
            \/ /\ relayRaftMsg[i][j].is_ready = TRUE
               /\ RelayRaftSend(i, j)
       ELSE UNCHANGED <<relayVars, msgs, txQueue>>
    /\ UNCHANGED <<vclock, term, wal>>

RelayNext(servers) == \E i, j \in servers: RelayProcess(i, j)

================================================================================
