## bugfix/replication

* Fixed a bug where the replication downstream could get stuck when the replica
  had `box.cfg.replication_synchro_queue_max_size` set to a smaller value than
  the master (gh-11836).
