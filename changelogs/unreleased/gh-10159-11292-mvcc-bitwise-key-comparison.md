## bugfix/memtx

* Fixed a bug that caused the memtx MVCC to miss conflicts over key definitions
  that used the number type or collations (gh-10159, gh-11292).
