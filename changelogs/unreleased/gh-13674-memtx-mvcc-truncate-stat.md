## bugfix/memtx

* Fixed non-zero tuple statistics and `bsize()` after truncating a space
  with a non-confirmed deletion when MVCC is enabled (gh-13674).
