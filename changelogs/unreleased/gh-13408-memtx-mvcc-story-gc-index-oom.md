## bugfix/memtx

* Fixed a crash in the MVCC story garbage collector when an index memory
  allocation failed while a read view (for example, a checkpoint) was open
  (gh-13408).
