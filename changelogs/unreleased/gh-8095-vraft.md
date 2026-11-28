## bugfix/replication

* The synchronous replication is switched to a new protocol - VRaft. It fixes
  several problems which could lead to split-brain errors and data forks even
  when the config had `replication.synchro_quorum` set to a majority (gh-8095).
