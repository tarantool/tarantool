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

EXTENDS Integers, FiniteSets, Sequences

--------------------------------------------------------------------------------
\* Declaration
--------------------------------------------------------------------------------

CONSTANTS
    Servers

ASSUME Cardinality(Servers) > 0

\* Wal implementation.
VARIABLES
    wal,     \* Sequence of log entries, persisted in WAL.
    walQueue \* Queue from TX thread to WAL.

\* Variables, defined in non imported modules.
VARIABLES
    txQueue  \* box module.

walVars == <<wal, walQueue>>

--------------------------------------------------------------------------------
\* Imports
--------------------------------------------------------------------------------

LOCAL INSTANCE utils

--------------------------------------------------------------------------------
\* Implementation
--------------------------------------------------------------------------------

WalInit ==
    /\ wal = [i \in Servers |-> << >>]
    /\ walQueue = [i \in Servers |-> << >>]

\* Implementation of the wal_write_to_disk, non failing.
WalProcess(i) ==
    /\ Len(walQueue[i]) > 0
    /\ LET entry == Head(walQueue[i])
           \* Implementation of the wal_assign_lsn.
           newRows == [j \in 1..Len(entry.rows) |->
                [entry.rows[j] EXCEPT !.lsn = IF entry.rows[j].lsn = -1 THEN
                 LastLsn(wal[i]) + j ELSE entry.rows[j].lsn]]
           \* Write to disk.
           newWal == wal[i] \o newRows
           \* Update txn only if it's not Nil.
           newEntry == [entry EXCEPT !.rows = newRows, !.complete_data =
                IF entry.complete_data # <<>> THEN [entry.complete_data EXCEPT
                !.stmts = newRows] ELSE entry.complete_data]
           newTxQueue == Append(txQueue[i], TxMsg(TxWalType, newEntry))
           newWalQueue == Tail(walQueue[i])
       IN /\ wal' = [wal EXCEPT ![i] = newWal] \* write to disk.
          /\ txQueue' = [txQueue EXCEPT ![i] = newTxQueue] \* send msg to Tx.
          /\ walQueue' = [walQueue EXCEPT ![i] = newWalQueue]

WalNext(servers) == \E i \in servers: WalProcess(i)

================================================================================
