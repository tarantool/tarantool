local ffi    = require('ffi')
local errno  = require('errno')
local buffer = require('buffer')

ffi.cdef[[
typedef struct iconv *iconv_t;
iconv_t iconv_open(const char *tocode, const char *fromcode);
void    iconv_close(iconv_t cd);
size_t  iconv(iconv_t cd, const char **inbuf, size_t *inbytesleft,
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
        local res = ffi.C.iconv(iconv, data_ptr, data_left,
                                buf_ptr, buf_left)
        if res == ffi.cast('size_t', -1) and errno() ~= E2BIG then
            ffi.C.iconv(iconv, nil, nil, nil, nil)
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
    ffi.C.iconv(iconv, nil, nil, nil, nil)
    local result = ffi.string(buf.rpos, buf:size())
    buf:reset()
    return result
end

local iconv_mt = {
    __call = iconv_convert,
    __gc = ffi.C.iconv_close,
    __tostring = function(iconv) return string.format("iconv: %p", iconv) end
}

ffi.metatype(iconv_t, iconv_mt)

local function iconv_new(to, from)
    if type(to) ~= 'string' or type(from) ~= 'string' then
        error('Usage: iconv.new("CP1251", "KOI8-R")')
    end
    local iconv = ffi.C.iconv_open(to, from)
    if iconv == conv_rv_error then
        error('iconv: '..errno.strerror())
    end
    return iconv;
end

return {
    new = iconv_new,
}
