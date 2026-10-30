## bugfix/core

* Fixed a signed integer overflow in datetime construction from a map that
  could make the computed epoch wrap around for years close to the minimum
  supported date (gh-13578).
