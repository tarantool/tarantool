------------------------------ MODULE relayTests -------------------------------

EXTENDS FiniteSets

CONSTANTS Servers, MaxHeartbeatsPerTerm

VARIABLES msgs, relaySentLsn, relayLastAck, relayRaftMsg, relayHeartbeatCtr,
          txQueue, vclock, wal, term
allVars == <<msgs, relaySentLsn, relayLastAck, relayRaftMsg, relayHeartbeatCtr,
             txQueue, vclock, wal, term>>

INSTANCE relay

ASSUME LET T == INSTANCE TLC IN T!PrintT("relayTests")

--------------------------------------------------------------------------------
\* Unit tests
--------------------------------------------------------------------------------

--------------------------------------------------------------------------------
\* Specification test
--------------------------------------------------------------------------------

Init == /\ RelayInit
        /\ msgs = [i \in Servers |-> [j \in Servers |-> [k \in 1..2 |-> <<>>]]]
        /\ txQueue = [i \in Servers |-> << >>]
        /\ vclock = [i \in Servers |-> [j \in Servers |-> 0]]
        /\ wal = [i \in Servers |-> << >>]
        /\ term = [i \in Servers |-> 0]

Next == RelayNext(Servers)

Spec == Init /\ [][Next]_allVars /\ WF_allVars(Next)

===============================================================================
