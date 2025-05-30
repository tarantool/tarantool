## feature/memtx

* Now it's possible to sort secondary keys using a new O(n) sorting
  algorithm using an additional data written into the snapshot. The
  feature can be enabled with a new `memtx_sort_data_enabled` option
  in `box.cfg` (gh-10847).
