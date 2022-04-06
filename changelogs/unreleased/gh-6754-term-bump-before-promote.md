## feature/raft

* Added `term` field to `box.info.synchro.queue`. It contains term of the
  last PROMOTE. It is usually equal to `box.info.election.term` but may be
  less than election term when new round of elections started, but no one
  promoted yet.
