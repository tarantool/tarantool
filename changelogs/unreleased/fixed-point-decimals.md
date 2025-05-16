## feature/core

* Added support for fixed point decimal types `decimal32`, `decimal64`,
  `decimal128` and `decimal256`.

## feature/app

* Decimal support is changed to fit new fixed point decimal types. Removed
  limit on exponent, precision is increased to 76 decimal digits and
  printing is done in scientific notation.
