## bugfix/replication

* Fixed a false split-brain error after demoting and promoting a leader whose
  replica had received synchronous queue metadata during JOIN (gh-13786).
