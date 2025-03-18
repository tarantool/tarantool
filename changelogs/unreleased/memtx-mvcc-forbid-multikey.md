## bugfix/memtx

 * Tarantool allowed to create multikey indexes with memtx MVCC enabled,
   but they were not supported - it led to a crash or a panic. Now
   Tarantool raises an error when one tries to create such index with
   memtx MVCC enabled (gh-6385).
