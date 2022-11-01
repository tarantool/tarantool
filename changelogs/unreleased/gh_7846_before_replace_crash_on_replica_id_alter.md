## bugfix/replication

* Say a warning when replica_id is changed by before_replace trigger while
  adding a new replica (gh-7846). There was an assertion checking this before.
  Also, handle the case when before_replace set on space `_cluster` returns
  nil - it caused segmentation fault before.
