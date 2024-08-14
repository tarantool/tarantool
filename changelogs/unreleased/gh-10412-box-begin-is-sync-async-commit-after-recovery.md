## bugfix/box

* Fixed a bug that caused synchronous transactions (created with
  `box.begin{is_sync = true}`) on asynchronous spaces to get committed
  asynchronously during recovery (gh-10412).
