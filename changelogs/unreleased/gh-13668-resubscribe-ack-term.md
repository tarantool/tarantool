## bugfix/replication

* Fixed a leader confirming synchronous transactions after a replica, which
  had voted for a new leader in a newer term, reconnected to it (gh-13668).
