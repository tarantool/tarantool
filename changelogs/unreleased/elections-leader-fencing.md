## feature/raft

 * Servers with elections enabled will resign leadership and become read-only
   when number of connected replicas becomes less than quorum. This should
   prevent split-brain in some situations (gh-6661).
