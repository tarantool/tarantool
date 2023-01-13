## bugfix/core

* **[Breaking change]** A `box_region_alloc` call that is not paired with
  a `box_region_truncate` call produces leaks. Previously, one could
  rely on some C API functions (like executing a DML statement) truncating
  fiber region to 0. Now, none of the box C API functions truncates memory
  it doesn't own (gh-5665).
