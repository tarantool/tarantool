## bugfix/memtx

* Fixed an out-of-bounds read on a delete of a tuple missing from a bitset
  index being built in the background, which could delete another tuple
  from the index (gh-13487).
