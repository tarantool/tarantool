## bugfix/vinyl

* Fixed the race between Vinyl garbage collection and compaction resulting in
  broken vylog and recovery (gh-5436).
