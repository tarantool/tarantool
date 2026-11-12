## bugfix/replication

* Fixed a startup failure after an anonymous replica crashes while
  registering: recovery now completes registration when an upstream
  already has the replica registered (gh-11039).
