## bugfix/raft

* Fixed an assertion failure in case when an election candidate is reconfigured
  to a voter during an ongoning WAL write (gh-8169).
