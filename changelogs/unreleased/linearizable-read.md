## feature/core

* Introduced a new transaction isolation level `linearizable`. Transactions
  started with `box.begin{txn_isolation = "linearizable"}` always see the latest
  data confirmed by the quorum (gh-6707).
