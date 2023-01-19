## bugfix/replication

* A warning is now raised when `replica_id` is changed by a `before_replace`
  trigger while adding a new replica. Previously, there was an assertion
  checking this (gh-7846).

* Fixed a segmentation fault that happened when a `before_replace` trigger set
  on space `_cluster` returned nil (gh-7846).
