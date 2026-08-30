## bugfix/replication

* Fixed an out-of-bounds write triggered by a replication peer sending an
  `IPROTO_RAFT` message whose vote contains an ID outside the valid replica ID
  range.
