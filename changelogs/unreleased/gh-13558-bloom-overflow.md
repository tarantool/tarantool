## bugfix/core

* Fixed an integer overflow in bloom filter creation that could make the
  computed table size wrap around to a tiny value for very large element
  counts or very small false positive rates (gh-13558).
