## bugfix/vinyl

* Fixed a bug when a deleted tuple wasn't purged from a secondary index data
  stored on disk in case it was updated more than once in the same transaction.
  The bug couldn't result in inconsistent query results, but it could lead to
  performance degradation and increased disk usage (gh-10820, gh-10822).
