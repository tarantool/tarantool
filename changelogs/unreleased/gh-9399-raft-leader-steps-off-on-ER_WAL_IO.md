## feature/raft

* Now the leader resigns on the first encounter with the `ER_WAL_IO`
  write error (gh-9399).
