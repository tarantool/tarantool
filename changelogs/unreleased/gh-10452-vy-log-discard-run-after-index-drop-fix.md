## bugfix/vinyl

* Fixed a bug when recovery could fail with the error "Invalid VYLOG file:
  Run XXXX deleted but not registered" or "Invalid VYLOG file: Run XXX deleted
  twice" in case a dump or compaction completed with a disk write error after
  the target index was dropped (gh-10452).
