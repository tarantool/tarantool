# bugfix/replication

* Now at replication no stop sending heartbeats during WAL scan. This prevents
  relay from timing out when a freshly subscribed replica needs rows from the
  end of a long `.xlog` (gh-9094).
