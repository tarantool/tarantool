## bugfix/core

* Fixed a redundant deprecation warning about `replication_connect_quorum`
  being logged when `bootstrap_strategy` is set to `'legacy'` explicitly in the
  same box.cfg (gh-12933).
