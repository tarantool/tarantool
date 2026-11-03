local ffi    = require('ffi')
local errno  = require('errno')
local buffer = require('buffer')
local cord_ibuf_take = buffer.internal.cord_ibuf_take
local cord_ibuf_put = buffer.internal.cord_ibuf_put

ffi.cdef[[
typedef struct iconv *iconv_t;
iconv_t tnt_iconv_open(const char *tocode, const char *fromcode);
void    tnt_iconv_close(iconv_t cd);
size_t  tnt_iconv(iconv_t cd, const char **inbuf, size_t *inbytesleft,
                   char **outbuf, size_t *outbytesleft);
]]

local iconv_t         = ffi.typeof('struct iconv')
local char_ptr_arr_t  = ffi.typeof('char *[1]')
local cchar_ptr_arr_t = ffi.typeof('const char *[1]')
local cchar_ptr_t     = ffi.typeof('const char *')
local size_t_arr_t    = ffi.typeof('size_t [1]')

local E2BIG    = errno['E2BIG']
local EINVAL   = errno['EINVAL']
local EILSEQ   = errno['EILSEQ']
local BUF_SIZE = 64

local conv_rv_error = ffi.cast('void *', -1)

---The `iconv` module provides a way to convert a string with one encoding
---to a string with another encoding, for example from ASCII to UTF-8. It is
---based on the POSIX iconv routines.
---
---An exact list of the available encodings may depend on environment.
---Typically the list includes ASCII, BIG5, KOI8R, LATIN8, MS-GREEK, SJIS,
---and about 100 others. For a complete list, type `iconv --list` on a
---terminal.
---
---@class iconv
---@field new fun(
---    to: string,
---    from: string
---): iconv.converter Construct a new iconv instance.
---
---    If either parameter is not a valid name, there will be an error
---    message.
---
---    **Example:**
---
---    ```tarantoolsession
---    tarantool> converter = require('iconv').new('UTF8', 'ASCII')
---    ---
---    ...
---    ```
---@field converter fun(
---    input_string: string
---): string Convert a string from one encoding to another.
---
---    If anything in `input_string` cannot be converted, there will be an
---    error message and the result string will be unchanged.
---
---    **Example:**
---
---    The Unicode code point U+0414 (CYRILLIC CAPITAL LETTER DE) is
---    encoded in UTF-8 as the bytes D0 94. Tarantool uses UTF-8, so use
---    `string.hex(string.char(208, 148))` to show the UTF-8 source bytes,
---    then convert the character to UTF-16BE and check the result. The
---    result 0414 shows that iconv conversion works. (Different iconv
---    implementations might use different names, for example UTF-16BE
---    instead of UTF16BE.)
---
---    ```tarantoolsession
---    tarantool> string.hex(string.char(208, 148))
---    ---
---    - d094
---    ...
---
---    tarantool> converter = require('iconv').new('UTF16BE', 'UTF8')
---    ---
---    ...
---
---    tarantool> utf16_string = converter(string.char(208, 148))
---    ---
---    ...
---
---    tarantool> string.hex(utf16_string)
---    ---
---    - '0414'
---    ...
---    ```

---@class iconv.converter: ffi.cdata*
---@operator call fun(self: iconv.converter, input_string: string): string

---@param data string the string to be converted (the "from" string)
---@return string res_string the string that results from the conversion
---    (the "to" string)
local function iconv_convert(iconv, data)
    if not ffi.istype(iconv_t, iconv) then
        error("Usage: iconv:convert(data: string)")
    end
    local data_len   = data:len()
    local data_ptr   = cchar_ptr_arr_t(cchar_ptr_t(data))
    local data_left  = size_t_arr_t(data_len)

    -- prepare at lease BUF_SIZE and at most data_len bytes in shared buffer
    local output_len = data_len >= BUF_SIZE and data_len or BUF_SIZE
    local buf      = cord_ibuf_take();
    local buf_ptr  = char_ptr_arr_t()
    local buf_left = size_t_arr_t()

    while data_left[0] > 0 do
        buf_ptr[0]  = buf:reserve(output_len)
        buf_left[0] = buf:unused()
        local res = ffi.C.tnt_iconv(iconv, data_ptr, data_left,
                                buf_ptr, buf_left)
        if res == ffi.cast('size_t', -1) then
            local err = errno()
            if err ~= E2BIG then
                cord_ibuf_put(buf)
                ffi.C.tnt_iconv(iconv, nil, nil, nil, nil)
                if err == EINVAL then
                    error('Invalid multibyte sequence')
                end
                if err == EILSEQ then
                    error('Incomplete multibyte sequence')
                end
                error('Unknown conversion error: ' .. errno.strerror(err))
            end
        end
        buf:alloc(buf:unused() - buf_left[0])
    end

    -- iconv function sets cd's conversion state to the initial state
    ffi.C.tnt_iconv(iconv, nil, nil, nil, nil)
    local result = ffi.string(buf.rpos, buf:size())
    cord_ibuf_put(buf)
    return result
end

local iconv_mt = {
    __call = iconv_convert,
    __tostring = function(iconv) return string.format("iconv: %p", iconv) end
}

ffi.metatype(iconv_t, iconv_mt)

---@param to string the name of the encoding that we will convert to.
---@param from string the name of the encoding that we will convert from.
---@return iconv.converter iconv a new iconv instance -- in effect,
---    a callable function
local function iconv_new(to, from)
    if type(to) ~= 'string' or type(from) ~= 'string' then
        error('Usage: iconv.new("CP1251", "KOI8-R")')
    end
    local iconv = ffi.C.tnt_iconv_open(to, from)
    if iconv == conv_rv_error then
        error('iconv: '..errno.strerror())
    end
    ffi.gc(iconv, ffi.C.tnt_iconv_close)
    return iconv;
end

return {
    new = iconv_new,
}
