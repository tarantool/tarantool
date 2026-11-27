## bugfix/replication

* Fixed a bug when increasing `replication_synchro_queue_max_size` or setting
  it to 0 did not wake up the transactions waiting for free space in a full
  synchronous transaction queue. They, and the later transactions queued
  behind them, could stay blocked until an unrelated queue event, or forever.
