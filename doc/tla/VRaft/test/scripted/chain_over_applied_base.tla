note: >-
  A base of a chain confirmed on its own moves the queue owner under the
  live chain. The next PROMOTE chained over it keeps the chain's previous
  owner, not the local one: the border it inherits is an LSN of that owner,
  and the new owner's transaction, judged by it, is rolled back everywhere.
  Five terms: the first owner's PROMOTE and transaction for the border, Q,
  R over the pending Q, the chain over R after Q applied.
data_nodes: 3
voters: 0
quorum: 2
max_term: 5
max_promotes: 4
transactions: 2

------------------------ MODULE chain_over_applied_base ------------------------

\* Term 2: n1 takes the limbo through its own PROMOTE and writes txn a, which
\* stays pending. Everyone has the rows.
NodeBumpTerm(n1)
Observe(n2, 2)
NodeVote(n2, n1)
NodeBecomeLeader(n1)
NodeStartPromote(n1)                 \* P1: n1 lsn 1
Deliver(n2, n1, 1)
NodeConfirmPromote(n1)               \* C1: n1 lsn 2
NodeCreateTransaction(n1)            \* a: n1 lsn 3
Expect(Nodes[n1].limbo.data = a)
Deliver(n2, n1, 2)
Deliver(n2, n1, 3)
Deliver(n3, n1, 1)
Deliver(n3, n1, 2)
Deliver(n3, n1, 3)

\* Term 3: n2 writes Q over the owner n1 with the border at a.
NodeBumpTerm(n2)
Observe(n3, 3)
NodeVote(n3, n2)
NodeBecomeLeader(n2)
NodeStartPromote(n2)                 \* Q: n2 lsn 1
Expect(Nodes[n2].limbo_promotions[n2].prev_owner = n1)
Expect(Nodes[n2].limbo_promotions[n2].confirm_lsn = 3)
Deliver(n3, n2, 1)
Deliver(n1, n2, 1)

\* Term 4: n3 chains R over the pending Q. R inherits the previous owner n1
\* and the border, and carries Q's term.
NodeBumpTerm(n3)
Observe(n1, 4)
NodeVote(n1, n3)
NodeBecomeLeader(n3)
NodeStartPromote(n3)                 \* R: n3 lsn 1
Expect(Nodes[n3].limbo_promotions[n3].prev_owner = n1)
Expect(Nodes[n3].limbo_promotions[n3].term_map[n2] = 3)
Deliver(n1, n3, 1)

\* Q gets confirmed on its own. n2 is the owner and writes txn b, which stays
\* pending. n1 and n3 apply Q with R still live: the owner is n2 there, the
\* chain still names n1.
NodeConfirmPromote(n2)               \* CQ: n2 lsn 2
NodeCreateTransaction(n2)            \* b: n2 lsn 3
Deliver(n1, n2, 2)
Expect(Nodes[n1].limbo_owner = n2)
Expect(PromoteIsPresent(Nodes[n1].limbo_promotions[n3]))
Deliver(n1, n2, 3)
Deliver(n3, n2, 2)
Deliver(n3, n2, 3)

\* R confirmed on n3: b is the new owner's, not R's previous owner's. Rejected.
NodeConfirmPromote(n3)               \* CR: n3 lsn 2
Expect(b \in Nodes[n3].data_rejected)

\* Term 5: n1 chains over the live R with Q applied and b in its limbo. The
\* new PROMOTE keeps R's previous owner, n1 itself, not the local owner n2.
NodeBumpTerm(n1)
Observe(n2, 5)
NodeVote(n2, n1)
NodeBecomeLeader(n1)
NodeStartPromote(n1)                 \* P2: n1 lsn 4
Expect(Nodes[n1].limbo_promotions[n1].prev_owner = n1)
Deliver(n2, n3, 1)                   \* R precedes P2 in n1's journal
Deliver(n2, n1, 4)

\* The chain confirmed on n1: b is rejected here too, the same verdict as
\* R's on n3. Then R's CONFIRM comes to n1 as a nop, and n1 is caught up with
\* n3 - the consistency invariant compares the two.
NodeConfirmPromote(n1)
Expect(b \in Nodes[n1].data_rejected)
Deliver(n1, n3, 2)

================================================================================
