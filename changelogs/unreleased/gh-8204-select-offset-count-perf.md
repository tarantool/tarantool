## bugfix/box

* Significantly improved the performance of `select` requests with the
  `offset` specified and `count` requests. This prevents DoS conditions
  previously possible (gh-8204).
