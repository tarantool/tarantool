# bugfix/vinyl

* Fix possible keys divergence during secondary index build which might
  lead to missing tuples in it (gh-6045).
