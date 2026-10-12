## bugfix/memtx

* Fixed an iterator over a multikey or functional index that, after the index
  was modified concurrently, returned some entries of the tuple it was paused
  on twice or skipped them (gh-13410).
