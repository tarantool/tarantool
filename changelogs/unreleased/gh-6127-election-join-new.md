## bugfix/raft

* Fixed an error when a new replica in a Raft cluster could try to join from a
  follower instead of a leader and failed with an error `ER_READONLY` (gh-6127).
