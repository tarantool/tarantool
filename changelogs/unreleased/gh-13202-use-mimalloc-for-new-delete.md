## feature/build

* Introduced the `ENABLE_MIMALLOC_NEW_DELETE` build option (enabled by default
  on Linux). When enabled, the bundled mimalloc allocator serves all C++
  `operator new`/`operator delete` calls process-wide. The `malloc`/`free`
  family used by C code stays on the system allocator. This speeds up workloads
  with many small short-lived allocations in C++ libraries (gh-13202).
