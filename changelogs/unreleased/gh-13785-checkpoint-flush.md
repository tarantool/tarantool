## bugfix/replication

* Fixed a crash when a snapshot fetch failed to flush pending synchronous
  transactions and those transactions subsequently completed (gh-13785).
