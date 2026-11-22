-- luacheck: ignore 212 unused arguments: this file only holds annotations.
---@diagnostic disable: missing-return

---The `utf8` module is Tarantool's module for handling UTF-8 strings. It
---includes some functions which are compatible with ones in Lua 5.3, but
---Tarantool has much more. For example, because internally Tarantool
---contains a complete copy of the "International Components For Unicode"
---library, there are comparison functions which understand the default
---ordering for Cyrillic (Capital Letter Zhe = Small Letter Zhe) and
---Japanese (Hiragana A = Katakana A).
local utf8 = {}

---Compare two strings with the Default Unicode Collation Element Table
---(DUCET) for the Unicode Collation Algorithm. Thus `'\u{E5}'` is less
---than 'B', even though the code-point value of `\u{E5}` (229) is greater
---than the code-point value of B (66), because the algorithm depends on
---the values in the Collation Element Table, not the code-point values.
---
---The comparison is done with primary weights. Therefore the elements
---which affect secondary or later weights (such as "case" in Latin or
---Cyrillic alphabets, or "kana differentiation" in Japanese) are ignored.
---If asked "is this like a Microsoft case-insensitive accent-insensitive
---collation" we tend to answer "yes", though the Unicode Collation
---Algorithm is far more sophisticated than those terms imply.
---
---**Example:**
---
---```lua
---utf8.casecmp('\u{E9}', 'e') -- 0
---utf8.casecmp('E', 'e') -- 0
---```
---@param s1 string a string encoded with UTF-8.
---@param s2 string a string encoded with UTF-8.
---@return integer result -1 meaning "less", 0 meaning "equal", +1 meaning
---    "greater".
function utf8.casecmp(s1, s2) end

---Compare two strings with the Default Unicode Collation Element Table
---(DUCET) for the Unicode Collation Algorithm. Thus `'\u{E5}'` is less
---than 'B', even though the code-point value of `\u{E5}` (229) is greater
---than the code-point value of B (66), because the algorithm depends on
---the values in the Collation Element Table, not the code values.
---
---The comparison is done with at least three weights. Therefore the
---elements which affect secondary or later weights (such as "case" in
---Latin or Cyrillic alphabets, or "kana differentiation" in Japanese) are
---not ignored, and upper case comes after lower case.
---
---**Example:**
---
---```lua
---utf8.cmp('\u{E9}', 'e') -- 1
---utf8.cmp('E', 'e') -- 1
---```
---@param s1 string a string encoded with UTF-8.
---@param s2 string a string encoded with UTF-8.
---@return integer result -1 meaning "less", 0 meaning "equal", +1 meaning
---    "greater".
function utf8.cmp(s1, s2) end

---Convert code points to a UTF-8 string. The code-point number is the
---value that corresponds to a character in the Unicode Character
---Database. This is not the same as the byte values of the encoded
---character, because the UTF-8 encoding scheme is more complex than a
---simple copy of the code-point number.
---
---Another way to construct a string with Unicode characters is with the
---`\u{hex-digits}` escape mechanism, for example `'\u{41}\u{42}'` and
---`utf8.char(65, 66)` both produce the string 'AB'.
---
---**Example:**
---
---```lua
---utf8.char(229) -- '\u{E5}'
---```
---@param code_point integer a Unicode code point value.
---@param ... integer additional Unicode code point values.
---@return string result the UTF-8 string.
function utf8.char(code_point, ...) end

---Return true if the input character is an "alphabetic-like" character,
---otherwise return false. Generally speaking a character will be
---considered alphabetic-like provided it is typically used within a word,
---as opposed to a digit or punctuation. It does not have to be a
---character in an alphabet.
---
---**Example:**
---
---```lua
---utf8.isalpha('\u{416}') -- true
---utf8.isalpha('\u{E5}') -- true
---utf8.isalpha('9') -- false
---```
---@param char string|integer a single UTF-8 character expressed as a
---    one-byte string or a code point value.
---@return boolean result whether the character is alphabetic-like.
function utf8.isalpha(char) end

---Return true if the input character is a digit, otherwise return false.
---
---**Example:**
---
---```lua
---utf8.isdigit('\u{416}') -- false
---utf8.isdigit('\u{E5}') -- false
---utf8.isdigit('9') -- true
---```
---@param char string|integer a single UTF-8 character expressed as a
---    one-byte string or a code point value.
---@return boolean result whether the character is a digit.
function utf8.isdigit(char) end

---Return true if the input character is lower case, otherwise return
---false.
---
---**Example:**
---
---```lua
---utf8.islower('\u{416}') -- false
---utf8.islower('\u{E5}') -- true
---utf8.islower('9') -- false
---```
---@param char string|integer a single UTF-8 character expressed as a
---    one-byte string or a code point value.
---@return boolean result whether the character is lower case.
function utf8.islower(char) end

---Return true if the input character is upper case, otherwise return
---false.
---
---**Example:**
---
---```lua
---utf8.isupper('\u{416}') -- true
---utf8.isupper('\u{E5}') -- false
---utf8.isupper('9') -- false
---```
---@param char string|integer a single UTF-8 character expressed as a
---    one-byte string or a code point value.
---@return boolean result whether the character is upper case.
function utf8.isupper(char) end

---Return the number of characters in the string, or between the start and
---end byte positions.
---
---Byte positions for start and end can be negative, which indicates
---"calculate from end of string" rather than "calculate from start of
---string".
---
---UTF-8 is a variable-size encoding scheme. Typically a simple Latin
---letter takes one byte, a Cyrillic letter takes two bytes, a
---Chinese/Japanese character takes three bytes, and the maximum is four
---bytes.
---
---**Example:**
---
---```lua
---utf8.len('G'), utf8.len('\u{436}') -- 1, 1
---string.len('G'), string.len('\u{436}') -- 1, 2
---```
---@param s string a string encoded with UTF-8.
---@param start_byte? integer byte position of the first character.
---@param end_byte? integer byte position where to stop.
---@return integer? count the number of characters in the string, or
---    between the start and end positions.
---@return integer|string? error a byte position of an invalid UTF-8
---    sequence, or an error message.
function utf8.len(s, start_byte, end_byte) end

---Convert a UTF-8 string to lower case.
---
---**Example:**
---
---```lua
---utf8.lower('\u{C5}\u{393}\u{DE}\u{416}ABCDEFG')
----- '\u{E5}\u{3B3}\u{FE}\u{436}abcdefg'
---```
---@param s string a string encoded with UTF-8.
---@return string result the same string in lower case.
function utf8.lower(s) end

---Return the byte position of the next character and its code point value.
---The `next` function is often used in a loop to get one character at a
---time from a UTF-8 string.
---
---**Example:**
---
---In the string `'\u{E5}a'` the first character is `\u{E5}`, it starts at
---position 1, it takes two bytes to store so the character after it will
---be at position 3, its Unicode code point value is (decimal) 229.
---
---```lua
----- Show next-character position and first-character code point.
---utf8.next('\u{E5}a', 1) -- 3, 229
---
----- Show the code point of every character.
---for position, codepoint in utf8.next, '\u{E5}a' do
---    print(codepoint) -- 229, then 97
---end
---```
---@param s string a string encoded with UTF-8.
---@param start_byte? integer byte position where to start within the
---    string, the default is 1.
---@return integer? position the byte position of the next character.
---@return integer? codepoint the code point value of the next character.
function utf8.next(s, start_byte) end

---Return the substring of a UTF-8 string.
---
---Character positions for start and end can be negative, which indicates
---"calculate from end of string" rather than "calculate from start of
---string".
---
---The default value for the end position is the length of the input
---string. Therefore, saying `utf8.sub('abc', 1)` will return 'abc', the
---same as the input string.
---
---**Example:**
---
---```lua
---utf8.sub('\u{E5}\u{3B3}\u{FE}\u{436}abcdefg', 5, 8) -- 'abcd'
---```
---@param s string a string encoded with UTF-8.
---@param start integer the position of the first character.
---@param end_? integer the position of the last character.
---@return string result the substring of the input value.
function utf8.sub(s, start, end_) end

---Convert a UTF-8 string to upper case.
---
---In rare cases the upper-case result may be longer than the lower-case
---input, for example `utf8.upper('\u{DF}')` is 'SS'.
---
---**Example:**
---
---```lua
---utf8.upper('\u{E5}\u{3B3}\u{FE}\u{436}abcdefg')
----- '\u{C5}\u{393}\u{DE}\u{416}ABCDEFG'
---```
---@param s string a string encoded with UTF-8.
---@return string result the same string in upper case.
function utf8.upper(s) end

return utf8
