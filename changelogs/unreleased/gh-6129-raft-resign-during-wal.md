## bugfix/raft

* Fixed a rare crash with the leader election enabled (any mode except `off`),
  which could happen if a leader resigned from its role at the same time as some
  other node was writing something related to the elections to WAL. The crash
  was in debug build and in the release build it would lead to undefined
  behaviour (gh-6129).
