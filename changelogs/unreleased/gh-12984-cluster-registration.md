## feature/replication

* Changes to replica registrations in `_cluster` now affect `box.info.id`,
  `box.info.name`, and registration fields in `box.info.replication` only after
  commit, including when read from the transaction making the changes
  (gh-13667).
