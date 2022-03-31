## feature/core

* Added new `memtx_allocator` option to `box.cfg{}` that allows to select the
  appropriate allocator for memtx tuples if necessary. Possible values are
  `system` for malloc allocator and `small` for default small allocator.

  Implemented system allocator, based on malloc: slab allocator that is used for
  tuples allocation has a certain disadvantage---it tends to unresolvable
  fragmentation on certain workloads (size migration). In this case, a user
  should be able to choose another allocator. System allocator based on malloc
  function, but restricted by the same quota as slab allocator. System
  allocator does not alloc all memory at start. Instead, it allocates memory as
  needed, checking that quota is not exceeded (gh-5419).
