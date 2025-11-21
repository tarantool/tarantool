## feature/core

* Added ULID support to Tarantool:
  - new Lua module `ulid`;
  - C API: `tt_ulid_create`, `tt_ulid_from_string`, `tt_ulid_to_string`;
  - monotonic ULID generator;
  - tests for string/binary conversion and ordering.
