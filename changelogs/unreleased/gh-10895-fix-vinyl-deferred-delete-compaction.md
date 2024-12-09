## bugfix/vinyl

* Fixed a bug when a deleted secondary index key wasn't purged on compaction
  of a space with the `defer_deletes` option enabled (gh-10895).
