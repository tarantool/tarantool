## feature/core

* Add new 'memtx_allocator' option to box.cfg{} which allows to
  select the appropriate allocator for memtx tuples if necessary.
  Possible values are "system" for malloc allocator and "small"
  for default small allocator.
  Implement system allocator, based on malloc: slab allocator, which
  is used for tuples allocation, has a certain disadvantage - it tends
  to unresolvable fragmentation on certain workloads (size migration).
  In this case user should be able to choose other allocator. System
  allocator based on malloc function, but restricted by the same qouta
  as slab allocator. System allocator does not alloc all memory at start,
  istead, it allocate memory as needed, checking that quota is not exceeded
  (gh-5419).

