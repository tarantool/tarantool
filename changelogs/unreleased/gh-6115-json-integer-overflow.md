## feature/lua

* Added the `decode_overflow` JSON option for integers outside
  `[-2^63, 2^64-1]`. The default, `clamp`, preserves the previous behavior:
  values below `-2^63` become `-2^63`, and values above `2^64-1` become
  `2^64-1`.
  Other modes are `error` (raise an error), `number` (convert to a Lua number
  with possible precision loss), `decimal` (convert as with
  `decimal.new()`, including rounding, or fail outside its range), `string`
  (preserve the integer text), and `nil` (return
  `json.NULL`, preserving object fields and array positions). The option works
  with both `json.cfg()` and the second argument of `json.decode()`. The
  `number` mode respects `decode_invalid_numbers` for results that overflow
  to infinity. Floating-point input and in-range integers are unchanged
  (gh-6115).
