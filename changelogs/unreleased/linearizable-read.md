## feature/core

* There is a new transaction isolation level now: "linearizable". Transactions
  started with `box.begin{txn_isolation = "linearizable"}` always see the latest
  data confirmed by the quorum (gh-6707).
