## feature/raft

* Servers with elections enabled will resign the leadership and become
  read-only when the number of connected replicas becomes less than a quorum.
  This should prevent split-brain in some situations (gh-6661).
