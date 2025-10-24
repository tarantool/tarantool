## bugfix/raft

* Forcing nodes with `is_enabled=false` to always broadcast
  `is_leader_seen=false`. This allows candidate nodes to immediately clear
  witness map bits for non-participating nodes, enabling elections to
  proceed with only active participants (gh-11981).
