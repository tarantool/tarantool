---@meta

---The `decimal` module has functions for working with exact numbers.
---This is important when numbers are large or even the slightest
---inaccuracy is unacceptable.
---
---For example, Lua calculates `0.16666666666667 * 6` with
---floating-point so the result is 1. But with the decimal module
---(using `decimal.new` to convert the number to decimal type)
---`decimal.new('0.16666666666667') * 6` is `1.00000000000002`.
---
---Decimal numbers have `N` digits of precision, that is, up to `N`
---significant digits are stored. In Tarantool 3.5 the precision was
---increased from `N = 38` to `N = 76`. Precision does not limit the
---magnitude. For example, `decimal.new('1e40') * decimal.new('1e40')`
---is `1E+80`.
---
---Tarantool supports the usual arithmetic and comparison operators
---`+`, `-`, `*`, `/`, `%`, `^`, `<`, `>`, `<=`, `>=`, `~=`, `==`.
---If an operation has both decimal and non-decimal operands, then the
---non-decimal operand is converted to decimal before the operation
---happens.
---
---Use `tostring(decimal-number)` to convert back to a string.
---
---A decimal operation will fail if its result is too large to
---represent (overflow). A decimal operation will fail if arithmetic
---is impossible (such as division by zero or square root of minus 1).
---A decimal operation will not fail if rounding of post-decimal
---digits is necessary to get `N`-digit precision.
local decimal = {}

---@alias decimal_like decimal|string|number

---A decimal number. The `__tostring` metamethod converts it to a
---string, so `tostring(d)` returns the number as a string.
---@class decimal: ffi.cdata*
---@operator unm: decimal
---@operator add(decimal_like): decimal
---@operator sub(decimal_like): decimal
---@operator mul(decimal_like): decimal
---@operator div(decimal_like): decimal
---@operator mod(decimal_like): decimal
---@operator pow(decimal_like): decimal
---@operator eq(decimal_like): boolean
---@operator lt(decimal_like): boolean
---@operator le(decimal_like): boolean

---Returns absolute value of a decimal number.
---
---**Example:** if `a` is `-1` then `decimal.abs(a)` returns `1`.
---
---@param n decimal_like
---@return decimal
function decimal.abs(n) end

---Returns e raised to the power of a decimal number.
---
---**Example:** if `a` is `1` then `decimal.exp(a)` returns:
---
---```
---2.718281828459045235360287471352662497757247093699959574966967627724076630354
---```
---
---Compare `math.exp(1)` from the Lua math library, which returns
---`2.718281828459`.
---
---@param n decimal_like
---@return decimal
function decimal.exp(n) end

---Returns `true` if the specified value is a decimal, and `false`
---otherwise.
---
---**Example:** if `a` is `123` then `decimal.is_decimal(a)` returns
---`false`. If `a` is `decimal.new(123)` then
---`decimal.is_decimal(a)` returns `true`.
---
---@param n any
---@return boolean
function decimal.is_decimal(n) end

---Returns natural logarithm of a decimal number.
---
---**Example:** if `a` is `1` then `decimal.ln(a)` returns `0`.
---
---@param n decimal_like
---@return decimal
function decimal.ln(n) end

---Returns base-10 logarithm of a decimal number.
---
---**Example:** if `a` is `100` then `decimal.log10(a)` returns `2`.
---
---@param n decimal_like
---@return decimal
function decimal.log10(n) end

---Returns the value of the input as a decimal number.
---
---**Example:** if `a` is `1E-1` then `decimal.new(a)` returns `0.1`.
---
---@param n decimal_like
---@return decimal
function decimal.new(n) end

---Returns the number of digits in a decimal number.
---
---**Example:** `decimal.precision(decimal.new('123.4560'))`
---returns `7`.
---
---@param n decimal
---@return integer
function decimal.precision(n) end

---Returns the number after possible rounding or padding.
---
---If the number of post-decimal digits is greater than `new_scale`,
---then rounding occurs. The rounding rule is: round half away from
---zero. If the number of post-decimal digits is less than
---`new_scale`, then padding of zeros occurs.
---
---If the requested scale cannot be represented within `N` digits of
---precision, the number is returned unchanged with its original scale;
---for example, `decimal.rescale(decimal.new('1.01'), 76)` returns
---`1.01`.
---
---**Example:** if `a` is `-123.4550` then `decimal.rescale(a, 2)`
---returns `-123.46`, and `decimal.rescale(a, 5)` returns `-123.45500`.
---
---@param n decimal
---@param new_scale integer
---@return decimal
function decimal.rescale(n, new_scale) end

---Returns the number of post-decimal digits in a decimal number.
---
---**Example:** `decimal.scale(decimal.new('123.4560'))`
---`4`.
---
---@param n decimal
---@return integer
function decimal.scale(n) end

---Returns the square root of a decimal number.
---
---**Example:** if `a` is `2` then `decimal.sqrt(a)` returns:
---
---```
---1.414213562373095048801688724209698078569671875376948073176679737990732478462
---```
---
---@param n decimal_like
---@return decimal
function decimal.sqrt(n) end

---Returns a decimal number after possible removing of trailing
---post-decimal zeros.
---
---**Example:** if `a` is `2.20200` then `decimal.trim(a)` returns
---`2.202`.
---
---@param n decimal
---@return decimal
function decimal.trim(n) end

---Rounds a decimal number to a specified number of post-decimal
---digits.
---
---If the operation fails, the number is left intact.
---
---@param n decimal
---@param scale integer
---@return decimal
function decimal.round(n, scale) end

return decimal
