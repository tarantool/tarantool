## feature/build

* Introduced the `ENABLE_BUNDLED_MIMALLOC` build option and the
  `MIMALLOC_OVERRIDE` build parameter (`none` or `cpp`). By default, the
  bundled mimalloc allocator is built and serves all C++ `operator new` and
  `operator delete` calls process-wide (`cpp`). The `malloc`/`free` family
  used by C code stays on the system allocator. This speeds up workloads with
  many small short-lived allocations in C++ libraries (gh-13202).
