## bugfix/vinyl

* Fixed a bug in the vinyl read iterator that could result in a significant
  performance degradation of range select requests in the presence of an intensive
  write workload (gh-5700).
