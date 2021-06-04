#feature/replication

 * Introduced `box.info.replication[n].downstream.lag` field to monitor
   state of replication. This member represents a lag between the main
   node writes a certain transaction to it's own WAL and a moment it
   receives an ack for this transaction from a replica (gh-5447).
