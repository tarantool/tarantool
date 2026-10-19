## bugfix/box

* **[Breaking change]** Fixed missing privilege verification. Creation of
  non-sandboxed function now requires the `execute` privilege on `universe`
  ([ghs-164](https://github.com/tarantool/security/issues/164)).
