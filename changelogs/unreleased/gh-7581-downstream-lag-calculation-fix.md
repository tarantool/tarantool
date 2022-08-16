## bugfix/replication

* Fixed `box.info.replication[id].downstream.lag` growing indefinitely on a
  server when it's not writing any new transactions (gh-7581).
