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

------------------------------ MODULE definitions ------------------------------

\* See raft's state variable.
Follower == "FOLLOWER"
Candidate == "CANDIDATE"
Leader == "LEADER"

\* XrowEntry types.
DmlType == "INSERT"
PromoteType == "PROMOTE"
ConfirmType == "CONFIRM"
RollbackType == "ROLLBACK"
RaftType == "RAFT"
OkType == "OK"
NopType == "NOP"

\* Group IDs of the XrowEntry.
DefaultGroup == "DEFAULT"
LocalGroup == "LOCAL"

\* Flags of the XrowEntry:
\* - wait_sync - true for any transaction, that would enter the limbo.
\* - wait_ack - true for a synchronous transaction.
DefaultFlags == [wait_sync |-> FALSE, wait_ack |-> FALSE] \* Async.
SyncFlags == [wait_sync |-> TRUE, wait_ack |-> TRUE]

\* Type of cbus messages to Tx thread.
TxWalType == "WAL"
TxRelayType == "RELAY"
TxApplierType == "APPLIER"

\* See msgs variable for description.
RelaySource == 1
ApplierSource == 2

\* Error codes.
SplitBrainError == "SPLIT_BRAIN"

\* Reserved value
Nil == "NIL"

===============================================================================
