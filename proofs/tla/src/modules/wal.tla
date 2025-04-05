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

---------------------------------- MODULE wal ----------------------------------

EXTENDS Integers, FiniteSets, Sequences, utils

--------------------------------------------------------------------------------
\* Declaration
--------------------------------------------------------------------------------

CONSTANTS Servers
ASSUME Cardinality(Servers) > 0

VARIABLES
    wal,  \* Imlemented in the current module.
    box   \* For access of the queue.

--------------------------------------------------------------------------------
\* Implementation
--------------------------------------------------------------------------------

WalInit ==
    wal = [i \in Servers |-> [
        rows |-> << >>, \* Sequence of log entries, persisted in wal.
        queue |-> << >> \* Queue from TX thread to WAL.
    ]]

LOCAL WalState(i) == [
    rows |-> wal[i].rows,
    queue |-> wal[i].queue,
    boxQueue |-> box[i].queue
]

LOCAL WalStateApply(i, state) ==
    /\ wal' = VarSet(i, "queue", state.queue,
              VarSet(i, "rows", state.rows, wal))
    /\ box' = VarSet(i, "queue", state.boxQueue, box)

\* Implementation of the wal_write_to_disk, non failing.
WalProcess(state) ==
    IF Len(state.queue) > 0
    THEN LET txn == Head(state.queue)
             \* Implementation of the wal_assign_lsn.
             newRows == [j \in 1..Len(txn.stmts) |->
                  [txn.stmts[j] EXCEPT !.lsn = IF txn.stmts[j].lsn = -1 THEN
                   LastLsn(state.rows) + j ELSE txn.stmts[j].lsn]]
             \* Write to disk.
             newWalRows == state.rows \o newRows
             newTxn == [txn EXCEPT !.stmts = newRows]
             newBoxQueue == Append(state.boxQueue, TxMsg(TxWalType, newTxn))
             newWalQueue == Tail(state.queue)
         IN [state EXCEPT
                !.rows = newWalRows,
                !.queue = newWalQueue,
                !.boxQueue = newBoxQueue
            ]
    ELSE state

WalNext(servers) == \E i \in servers:
    WalStateApply(i, WalProcess(WalState(i)))

================================================================================
