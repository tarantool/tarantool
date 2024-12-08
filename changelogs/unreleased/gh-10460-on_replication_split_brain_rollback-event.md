## feature/box

* Added a new `box.ctl.on_replication_split_brain_rollback` event that occurs
  when an asynchronously committed synchronous transaction gets rolled back by a
  PROMOTE request to prevent a split brain (gh-10460).
