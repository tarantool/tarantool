## bugfix/replication

* Now if join fails with some non-critical error, like `ER_READONLY`,
  `ER_ACCESS_DENIED` or something network-related, the instance tries
  to find a new master to join off and tries again (gh-6126, gh-8681).

* States when joining a replica are renamed. So now when querying
  `box.info.replication[id].upstream.status` during join, you will
  see either `wait_snapshot` or `fetch_snapshot` instead of
  `initial_join` (gh-6126).
