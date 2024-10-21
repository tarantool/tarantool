## feature/config

- Now, when running in the supervised mode the bootstrap leader
  automatically goes read-only mode after the specified lease interval
  to let the coordinator control the master selection (gh-10419).
