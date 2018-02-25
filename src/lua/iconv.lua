local ffi    = require('ffi')
local errno  = require('errno')
local buffer = require('buffer')

ffi.cdef[[
typedef struct iconv *iconv_t;
iconv_t iconv_open(const char *tocode, const char *fromcode);
void    iconv_close(iconv_t cd);
size_t  iconv(iconv_t cd, const char **inbuf, size_t *inbytesleft,
              char **outbuf, size_t *outbytesleft);
/*
 * add prefix 'lib' under FreeBSD
 */
iconv_t libiconv_open(const char *tocode, const char *fromcode);
void    libiconv_close(iconv_t cd);
size_t  libiconv(iconv_t cd, const char **inbuf, size_t *inbytesleft,
                 char **outbuf, size_t *outbytesleft);
]]

local iconv_t         = ffi.typeof('struct iconv')
local char_ptr_arr_t  = ffi.typeof('char *[1]')
local cchar_ptr_arr_t = ffi.typeof('const char *[1]')
local cchar_ptr_t     = ffi.typeof('const char *')
local size_t_arr_t    = ffi.typeof('size_t [1]')

local _iconv_open
local _iconv_close
local _iconv

-- To fix #3073, BSD iconv implementation is not fully
-- compatible with iconv, so use external iconv.so lib
if jit.os == 'BSD' then
    _iconv_open = ffi.C.libiconv_open
    _iconv_close = ffi.C.libiconv_close
    _iconv = ffi.C.libiconv
else
    _iconv_open = ffi.C.iconv_open
    _iconv_close = ffi.C.iconv_close
    _iconv = ffi.C.iconv
end

local E2BIG    = errno['E2BIG']
local EINVAL   = errno['EINVAL']
local EILSEQ   = errno['EILSEQ']
local BUF_SIZE = 64

local conv_rv_error = ffi.cast('void *', -1)

local function iconv_convert(iconv, data)
    if not ffi.istype(iconv_t, iconv) then
        error("Usage: iconv:convert(data: string)")
    end
    local data_len   = data:len()
    local data_ptr   = cchar_ptr_arr_t(cchar_ptr_t(data))
    local data_left  = size_t_arr_t(data_len)

    -- prepare at lease BUF_SIZE and at most data_len bytes in shared buffer
    local output_len = data_len >= BUF_SIZE and data_len or BUF_SIZE
    local buf      = buffer.IBUF_SHARED;
    local buf_ptr  = char_ptr_arr_t()
    local buf_left = size_t_arr_t()
    buf:reset()

    while data_left[0] > 0 do
        buf_ptr[0]  = buf:reserve(output_len)
        buf_left[0] = buf:unused()
        local res = _iconv(iconv, data_ptr, data_left,
                                buf_ptr, buf_left)
        if res == ffi.cast('size_t', -1) and errno() ~= E2BIG then
            _iconv(iconv, nil, nil, nil, nil)
            if errno() == EINVAL then
                error('Invalid multibyte sequence')
            end
            if errno() == EILSEQ then
                error('Incomplete multibyte sequence')
            end
            error('Unknown conversion error: ' .. errno.strerror())
        end
        buf:alloc(buf:unused() - buf_left[0])
    end

    -- iconv function sets cd's conversion state to the initial state
    _iconv(iconv, nil, nil, nil, nil)
    local result = ffi.string(buf.rpos, buf:size())
    buf:reset()
    return result
end

local iconv_mt = {
    __call = iconv_convert,
    __gc = _iconv_close,
    __tostring = function(iconv) return string.format("iconv: %p", iconv) end
}

ffi.metatype(iconv_t, iconv_mt)

local function iconv_new(to, from)
    if type(to) ~= 'string' or type(from) ~= 'string' then
        error('Usage: iconv.new("CP1251", "KOI8-R")')
    end
    local iconv = _iconv_open(to, from)
    if iconv == conv_rv_error then
        error('iconv: '..errno.strerror())
    end
    return iconv;
end

return {
    new = iconv_new,
}
