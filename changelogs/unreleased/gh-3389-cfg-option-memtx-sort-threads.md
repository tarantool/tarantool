## feature/box

* Added the `box.cfg.memtx_sort_threads` parameter that specifies the number of
  threads used to sort indexes keys on loading a memtx database. OpenMP is
  not used to sort keys anymore (gh-3389).
