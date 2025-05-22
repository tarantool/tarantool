## bugfix/memtx

* Disallowed alteration of the primary index in a space with
  non-unique or nullable secondary indexes because such alters
  would crash Tarantool (gh-10951).
