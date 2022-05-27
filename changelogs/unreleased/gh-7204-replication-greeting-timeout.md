## bugfix/replication

* Fixed the bug because of which the error reason was not logged on a replica
  in case when the master didn't send a greeting message (gh-7204).
