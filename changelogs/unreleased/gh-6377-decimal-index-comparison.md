## bugfix/core

* Fixed a crash and possible undefined behaviour when using `scalar` and `number` indexes
  over fields containing both decimals and double `Inf` or `NaN`.

  For vinyl spaces, the above conditions could lead to wrong ordering of
  indexed values. To fix the issue, recreate the indexes on such
  spaces following this [guide](https://github.com/tarantool/tarantool/wiki/Fix-wrong-order-of-decimals-and-doubles-in-indices) (gh-6377).
