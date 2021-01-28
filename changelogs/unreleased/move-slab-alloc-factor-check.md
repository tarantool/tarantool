## feature/core

* In the previous version, if user entered an incorrect
  slab_alloc_factor value, the entered value was silently
  corrected to a valid one and tarantool continued to work.
  Moved slab_alloc_factor check to box_check_config()
  and if user entered incorrect slab_alloc_factor value,
  tarantool failed to start with error message.
