## bugfix/lua

* Raise an error when decoding a JSON integer outside the supported range
  `[-2^63, 2^64-1]` instead of silently clamping it to the nearest boundary
  (gh-6115).
