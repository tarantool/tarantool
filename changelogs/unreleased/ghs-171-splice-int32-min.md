## bugfix/box

* Fixed an integer signed overflow in the splice bounds check that allowed
  `INT32_MIN` offset to bypass validation, resulting in an out-of-bounds
  `memcpy()` crash
  ([ghs-171](https://github.com/tarantool/security/issues/171)).
