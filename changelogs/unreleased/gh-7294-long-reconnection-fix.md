## bugfix/replication

* Fixed a bug related to `box.info.replication[...].upstream` being stuck in the "connecting"
  state for several minutes after a replica DNS record change (gh-7294).
