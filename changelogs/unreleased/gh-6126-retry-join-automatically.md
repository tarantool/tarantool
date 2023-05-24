## bugfix/replication

* Now if a join fails with some non-critical error, such as `ER_READONLY`,
  `ER_ACCESS_DENIED`, or something network-related, the instance tries
  to find a new master to join off and tries again (gh-6126, gh-8681).

* States when joining a replica are renamed. Now the value of
  `box.info.replication[id].upstream.status` during join can be either
 `wait_snapshot` or `fetch_snapshot` instead of `initial_join` (gh-6126).
