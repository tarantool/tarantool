---@meta
-- luacheck: ignore 121 212
---
---The `string` module has everything in the standard Lua string library,
---and some Tarantool extensions.
---
---In this section we only discuss the additional functions that the
---Tarantool developers have added.
---@class string
---
---@field split fun(input_string: string, split_string?: string,
---    max?: integer): string[] @Split a string into parts.
---@field ljust fun(input_string: string, width: integer,
---    pad_character?: string): string @Left-justify a string.
---@field rjust fun(input_string: string, width: integer,
---    pad_character?: string): string @Right-justify a string.
---@field center fun(input_string: string, width: integer,
---    pad_character?: string): string @Center a string.
---@field startswith fun(input_string: string, start_string: string,
---    start_pos?: integer, end_pos?: integer): boolean @Check for a prefix.
---@field endswith fun(input_string: string, end_string: string,
---    start_pos?: integer, end_pos?: integer): boolean @Check for a suffix.
---@field hex fun(input_string: string): string @Return hex representation.
---@field fromhex fun(
---    hexadecimal_input_string: string): string @Decode hex digits.
---@field strip fun(input_string: string,
---    list_of_characters?: string): string @Strip both ends.
---@field lstrip fun(input_string: string,
---    list_of_characters?: string): string @Strip the left side.
---@field rstrip fun(input_string: string,
---    list_of_characters?: string): string @Strip the right side.

local string = {}

---Split input-string into one or more output strings in a table.
---
---The places to split are the places where split-string occurs.
---
---**Example:**
---
--- ```lua
--- string.split("A:B:C:D:F", ":", 2) -- {'A', 'B', 'C:D:F'}
--- ```
---
---@param inp string the string to split.
---@param sep? string the string to find within input-string. The
---    default is a space.
---@param max? integer the maximum number of delimiters to process counting
---    from the beginning of the input string. The result will contain
---    max + 1 parts maximum.
---@return string[] result the table of strings that were split from
---    input-string.
function string.split(inp, sep, max) end

---Return the string left-justified in a string of length `width`.
---
---The input string is never truncated.
---
---**Example:**
---
--- ```lua
--- string.ljust(' A', 5) -- ' A   '
--- ```
---
---@param inp string the string to left-justify.
---@param width integer the width of the string after left-justifying.
---@param char? string a single character to pad with, the default
---    is a space.
---@return string result the left-justified string, unchanged if
---    width <= string length.
function string.ljust(inp, width, char) end

---Return the string right-justified in a string of length `width`.
---
---The input string is never truncated.
---
---**Example:**
---
--- ```lua
--- string.rjust('', 5, 'X') -- 'XXXXX'
--- ```
---
---@param inp string the string to right-justify.
---@param width integer the width of the string after right-justifying.
---@param char? string a single character to pad with, the default
---    is a space.
---@return string result the right-justified string, unchanged if
---    width <= string length.
function string.rjust(inp, width, char) end

---Center a string in a string of length `width`.
---
---The input string is never truncated.
---
---**Example:**
---
--- ```lua
--- string.center("help", 6, '.') -- '.help.'
--- ```
---
---@param inp string the string to center.
---@param width integer the width of the string after centering.
---@param char? string a single character to pad with, the default
---    is a space.
---@return string result the centered string, unchanged if
---    width <= string length.
function string.center(inp, width, char) end

---Check that a string (or substring) starts with a given string.
---
---Optionally restricting the matching with the given offsets.
---
---**Example:**
---
--- ```lua
--- string.startswith(' A', 'A', 2, 5) -- true
--- ```
---
---@param inp string the string where start-string should be looked
---    for.
---@param head string the string to look for.
---@param _start? integer the position where to start looking within
---    input-string. May be negative, meaning the position should be
---    calculated from the end of the string.
---@param _end? integer the position where to end looking within
---    input-string. May be negative, meaning the position should be
---    calculated from the end of the string.
---@return boolean result true if input-string starts with start-string,
---    otherwise false.
function string.startswith(inp, head, _start, _end) end

---Check that a string (or substring) ends with a given string.
---
---Optionally restricting the matching with the given offsets.
---
---**Example:**
---
--- ```lua
--- string.endswith('Baa', 'aa') -- true
--- ```
---
---@param inp string the string where end-string should be looked
---    for.
---@param tail string the string to look for.
---@param _start? integer the position where to start looking within
---    input-string. May be negative, meaning the position should be
---    calculated from the end of the string.
---@param _end? integer the position where to end looking within
---    input-string. May be negative, meaning the position should be
---    calculated from the end of the string.
---@return boolean result true if input-string ends with end-string,
---    otherwise false.
function string.endswith(inp, tail, _start, _end) end

---Return the hexadecimal value of the input string.
---
---**Example:**
---
--- ```lua
--- string.hex('ABC ') -- '41424320'
--- ```
---
---@param inp string the string to process.
---@return string result the hexadecimal value, 2 hex-digit characters for
---    each input character.
function string.hex(inp) end

---Given a string containing pairs of hexadecimal digits, return a string
---with one byte for each pair.
---
---This is the reverse of `string.hex()`.
---
---The hexadecimal-input-string must contain an even number of hexadecimal
---digits.
---
---**Example:**
---
--- ```lua
--- string.fromhex('41424320') -- 'ABC '
--- ```
---
---@param inp string a string with pairs of hexadecimal
---    digits.
---@return string result a string with one byte for each pair of hexadecimal
---    digits.
function string.fromhex(inp) end

---Return the value of the input string, after removing characters on the
---left and the right.
---
---The optional list-of-characters parameter is a set not a sequence, so
---`string.strip(...,'ABC')` does not mean strip 'ABC', it means strip 'A' or
---'B' or 'C'.
---
---**Example:**
---
--- ```lua
--- string.strip(' ABC ') -- 'ABC'
--- ```
---
---@param inp string the string to process.
---@param chars? string what characters can be stripped. The
---    default is a space.
---@return string result the result after stripping characters from the
---    input string.
function string.strip(inp, chars) end

---Return the value of the input string, after removing characters on the
---left.
---
---The optional list-of-characters parameter is a set not a sequence, so
---`string.lstrip(...,'ABC')` does not mean strip 'ABC', it means strip 'A'
---or 'B' or 'C'.
---
---**Example:**
---
--- ```lua
--- string.lstrip(' ABC ') -- 'ABC '
--- ```
---
---@param inp string the string to process.
---@param chars? string what characters can be stripped. The
---    default is a space.
---@return string result the result after stripping characters from the
---    input string.
function string.lstrip(inp, chars) end

---Return the value of the input string, after removing characters on the
---right.
---
---The optional list-of-characters parameter is a set not a sequence, so
---`string.rstrip(...,'ABC')` does not mean strip 'ABC', it means strip 'A'
---or 'B' or 'C'.
---
---**Example:**
---
--- ```lua
--- string.rstrip(' ABC ') -- ' ABC'
--- ```
---
---@param inp string the string to process.
---@param chars? string what characters can be stripped. The
---    default is a space.
---@return string result the result after stripping characters from the
---    input string.
function string.rstrip(inp, chars) end

return string
