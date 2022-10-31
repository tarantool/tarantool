## bugfix/core

* **[Breaking change]** `box_region_alloc` not paired with
  `box_region_truncate` will produce leaks. Previously, one could
  rely on some C API functions (like executing a DML statement) truncating
  fiber region to 0. Now, none of the box C API functions truncates memory
  it doesn't own (gh-5665).
