## feature/raft

* Servers with elections enabled won't start new elections as long as at least
  one of their peers sees the current leader. They also won't start the
  elections when they don't have a quorum of connected peers. This should
  reduce cases when a server which has lost connectivity to the leader disrupts
  the whole cluster by starting new elections (gh-6654).

* Added the `leader_idle` field to `box.info.election` table. The value shows
  time in seconds since the last communication with a known leader (gh-6654).

## bugfix/raft

* Fixed `box.ctl.promote()` entering an infinite election loop when a node
  doesn't have enough peers to win the elections (gh-6654).
