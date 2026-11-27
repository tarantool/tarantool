## feature/box

* Added `box.ctl.txn_sync()` to wait for transactions already committing when
  the call starts, including their local WAL writes and synchronous
  confirmation (gh-11248).
