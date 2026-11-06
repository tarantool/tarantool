## bugfix/replication

* Fixed a panic when JOIN or FETCH_SNAPSHOT is interrupted after receiving
  the snapshot vclock. The transfer is retried if no metadata or data has
  been applied; otherwise startup aborts on the original error (gh-10737).
