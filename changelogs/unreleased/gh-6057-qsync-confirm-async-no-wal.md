## bugfix/replication

* Fixed a possible crash when a synchronous transaction was followed by an
  asynchronous transaction right when its confirmation was being written
  (gh-6057).
