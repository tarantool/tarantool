## feature/replication

* Added support for automatic rollback of asynchronous transactions from an old
  term when using synchronous replication. The feature is controlled by a new
  `replication_split_brain_handling_mode` enumeration option. Currently,
  the only available mode other than `none` (default) is `rollback`, which
  automatically rolls back conflicting asynchronous transactions in the event of
  a split brain situation (gh-10459).
