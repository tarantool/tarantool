## feature/replication

* Show a new `downstream.status` value `joining` in `box.info.replication` and
  `box.info.replication_anon()` of the master. It means that the master is
  sending a read view (initial data or a snapshot) to the replica, so the
  replica has not started following the master yet (gh-13554).
