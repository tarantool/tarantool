## bugfix/replication

* Fixed an out-of-bounds write triggered by a replication peer sending an
  `IPROTO_RAFT` message whose vote holds an id outside the valid replica id
  range (gh-noticket).
