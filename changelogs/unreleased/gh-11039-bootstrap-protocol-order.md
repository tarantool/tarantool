## bugfix/replication

* Changed the bootstrap protocol for named replicas (gh-11039). A named
  replica now fetches the master's snapshot, creates its initial checkpoint,
  subscribes to the master anonymously until its lag is small, and only then
  registers (REGISTER) and re-subscribes as a named replica. Previously a
  single JOIN connection registered the replica in the middle of the data
  stream, which could deadlock the master and the replica when a synchronous
  transaction was confirmed after the JOIN stop vclock. The new order is used
  only when the master supports it (Tarantool 3.8.0+); otherwise the legacy
  JOIN protocol is preserved. If such a named replica restarts after creating
  its initial checkpoint but before registering, it no longer panics or fails
  with "unknown replica" in `box.cfg()`: it recovers as a temporary anonymous
  replica, catches up with the master and registers within `box.cfg()`, so the
  call still returns with a non-zero `box.info.id`.
