-- digest.lua (internal file)

do

local ffi = require 'ffi'

ffi.cdef[[
    /* from openssl/sha.h */
    unsigned char *SHA(const unsigned char *d, size_t n, unsigned char *md);
    unsigned char *SHA1(const unsigned char *d, size_t n, unsigned char *md);
    unsigned char *SHA224(const unsigned char *d, size_t n,unsigned char *md);
    unsigned char *SHA256(const unsigned char *d, size_t n,unsigned char *md);
    unsigned char *SHA384(const unsigned char *d, size_t n,unsigned char *md);
    unsigned char *SHA512(const unsigned char *d, size_t n,unsigned char *md);
    unsigned char *MD4(const unsigned char *d, size_t n, unsigned char *md);
    unsigned char *SHA1internal(const unsigned char *d, size_t n, unsigned char *md);

    /* from openssl/md5.h */
    unsigned char *MD5(const unsigned char *d, size_t n, unsigned char *md);

    /* from libc */
    int snprintf(char *str, size_t size, const char *format, ...);


    typedef uint32_t (*crc32_func)(uint32_t crc,
        const unsigned char *buf, unsigned int len);
    extern crc32_func crc32_calc;

   /* base64 */
   int base64_decode(const char *in_base64, int in_len, char *out_bin, int out_len);
   int base64_encode(const char *in_bin, int in_len, char *out_base64, int out_len);
]]

local ssl
if ssl == nil then
    local variants = {
        'libssl.so.1.0.0',
        'libssl.so.0.9.8',
        'libssl.so',
        'ssl',
    }

    for _, libname in pairs(variants) do
        pcall(function() ssl = ffi.load(libname) end)
        if ssl ~= nil then
            break
        end
    end
end


local def = {
    sha     = { 'SHA',    20 },
--     sha1    = { 'SHA1',   20 },
    sha224  = { 'SHA224', 28 },
    sha256  = { 'SHA256', 32 },
    sha384  = { 'SHA384', 48 },
    sha512  = { 'SHA512', 64 },
    md5     = { 'MD5',    16 },
    md4     = { 'MD4',    16 }
}

local hexres = ffi.new('char[129]')

local function tohex(r, size)
    for i = 0, size - 1 do
        ffi.C.snprintf(hexres + i * 2, 3, "%02x",
            ffi.cast('unsigned int', r[i]))
    end
    return ffi.string(hexres, size * 2)
end

local m = {
    base64_encode = function(bin)
        if bin == nil then
            error('Usage: base64.encode(str)')
        else
            -- note: need add check size of bin, bin might containe any binary data
            if type(bin) == 'string' then
                local blen = string.len(bin)
                local slen = math.ceil(blen * 4 / 3) + 2
                local so  = ffi.new('char[?]', slen)
                local len = ffi.C.base64_encode(bin, blen, so, slen)
                bin = ffi.string(so, len)
            else
                bin = ''
            end
        end
        return bin
    end,

    base64_decode = function(data)
        if type(data) ~= 'string' then
            data = ''
        else
            local slen = string.len(data)
            local blen = math.ceil(slen * 3 / 4)
            local bo  = ffi.new('char[?]', blen)
            local len = ffi.C.base64_decode(data, slen, bo, blen)
            data = ffi.string(bo, len)
        end
        return data
    end,

    crc32 = function(str)
        if str == nil then
            str = ''
        else
            str = tostring(str)
        end
        return ffi.C.crc32_calc(4294967295, str, string.len(str))
    end,

    crc32_update = function(crc, str)
        if str == nil then
            str = ''
        else
            str = tostring(str)
        end
        return ffi.C.crc32_calc(tonumber(crc), str, string.len(str))
    end,

    sha1 = function(str)
        if str == nil then
            str = ''
        else
            str = tostring(str)
        end
        local r = ffi.C.SHA1internal(str, #str, nil)
        return ffi.string(r, 20)
    end,

    sha1_hex = function(str)
        if str == nil then
            str = ''
        else
            str = tostring(str)
        end
        local r = ffi.C.SHA1internal(str, #str, nil)
        return tohex(r, 20)
    end
}

if ssl ~= nil then

    for pname, df in pairs(def) do
        local hfunction = df[1]
        local hsize = df[2]

        m[ pname ] = function(str)
            if str == nil then
                str = ''
            else
                str = tostring(str)
            end
            local r = ssl[hfunction](str, string.len(str), nil)
            return ffi.string(r, hsize)
        end
        
        m[ pname .. '_hex' ] = function(str)
            if str == nil then
                str = ''
            else
                str = tostring(str)
            end
            local r = ssl[hfunction](str, string.len(str), nil)
            return tohex(r, hsize)
        end
    end
else
    local function errorf()
        error("libSSL was not loaded")
    end
    for pname, df in pairs(def) do
        m[ pname ] = errorf
        m[ pname .. '_hex' ] = errorf
    end
end

return m

end
