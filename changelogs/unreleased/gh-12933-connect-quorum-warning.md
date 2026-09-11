## bugfix/core

* Fixed a bug where a redundant deprecation warning about
  `replication_connect_quorum` was logged when `bootstrap_strategy` was
  explicitly set to `'legacy'` in the same `box.cfg()` call (gh-12933).
