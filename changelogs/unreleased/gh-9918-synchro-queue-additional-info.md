## feature/replication

* Added the `age` and `confirm_lag` fields to `box.info.synchro.queue`: the
  former shows the time that the oldest entry currently present in the queue has
  spent waiting for the quorum, while the latter shows the time that the latest
  successfully confirmed entry waited for the quorum to gather (gh-9918).
