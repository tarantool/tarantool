## feature/replication

* Introduced `box.info.replication[n].downstream.lag` field to monitor
  state of replication. This member represents a lag between the main
  node writing a certain transaction to its own WAL and a moment it
  receives an ack for this transaction from a replica (gh-5447).
