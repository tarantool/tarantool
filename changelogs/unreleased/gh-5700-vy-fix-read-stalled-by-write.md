## bugfix/vinyl

* Fixed a bug in Vinyl read iterator that could result in a significant
  performance degradation of range select requests in presence of an intensive
  write workload (gh-5700).
