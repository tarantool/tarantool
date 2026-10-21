## bugfix/box

* Fixed a signed integer overflow in the splice bounds check that allowed
  an `INT32_MIN` offset to bypass validation, resulting in an out-of-bounds
  `memcpy()` crash (ghs-171).
