## feature/build

* Added the `ENABLE_MIMALLOC` build option (enabled by default on Linux,
  disabled under ASAN/valgrind): the bundled mimalloc allocator overrides the
  standard `malloc`/`operator new` in the whole Tarantool process. This speeds
  up workloads with many small short-lived allocations that use system allocator
  (gh-13202).
