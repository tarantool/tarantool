## bugfix/core

* Fixed a bug with Tarantool C API freeing fiber region allocations it does not
  allocate. This could lead to use-after-free in client code which allocates
  memory on fiber region. (gh-5665).
