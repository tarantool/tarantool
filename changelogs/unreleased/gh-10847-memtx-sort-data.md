## feature/memtx

* Now it is possible to sort secondary keys with a new O(n) sorting
  algorithm that uses additional data written into the snapshot. The
  feature can be enabled with a new `memtx_sort_data_enabled` option
  in `box.cfg` (gh-10847).
