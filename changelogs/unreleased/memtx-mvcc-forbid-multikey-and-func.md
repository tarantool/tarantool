## bugfix/memtx

* Tarantool allowed to create multikey and functional indexes with
  memtx MVCC enabled, but they were not supported. This led to a crash
  or a panic. Now Tarantool raises an error when one tries to create
  an index of the kind with memtx MVCC enabled (gh-6385, gh-11099).
