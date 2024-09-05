## feature/replication

* Made asynchronous transactions bump the confirmation boundary of the
  synchronous queue allowing to detect a split-brain situation on an old
  synchronous queue owner when he receives a PROMOTE request that does not
  confirm the asynchronous transactions he has committed (gh-10528).
