## feature/box

* Added a new `box.ctl.on_replication_split_brain_rollback` event that occurs
  when asynchronously committed transaction stored in the synchronous
  transaction queue get rolled back by a PROMOTE request to prevent split brain
  (gh-10460).
