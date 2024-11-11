## bugfix/vinyl

* Fixed a bug when `index.stat()` and `index.len()` could report a wrong number
  of in-memory statements for a non-unique multi-key index of a space with
  the `defer_deletes` option enabled (gh-10751).
