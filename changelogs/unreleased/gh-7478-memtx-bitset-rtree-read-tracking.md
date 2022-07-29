## bugfix/memtx

* Fixed isolation level of **memtx** **BITSET** and **RTREE** indexes from 'read
  committed' to 'serializable' (gh-7478).
