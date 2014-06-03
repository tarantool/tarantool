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

    /* from openssl/md5.h */
    unsigned char *MD5(const unsigned char *d, size_t n, unsigned char *md);

    /* from libc */
    int snprintf(char *str, size_t size, const char *format, ...);


    typedef uint32_t (*crc32_func)(uint32_t crc,
        const unsigned char *buf, unsigned int len);
    extern crc32_func crc32_calc;
]]

local ssl
if ssl == nil then
    pcall(function() ssl = ffi.load('ssl') end)
end


local def = {
    sha     = { 'SHA',    20 },
    sha1    = { 'SHA1',   20 },
    sha224  = { 'SHA224', 28 },
    sha256  = { 'SHA256', 32 },
    sha384  = { 'SHA384', 48 },
    sha512  = { 'SHA512', 64 },
    md5     = { 'MD5',    16 },
    md4     = { 'MD4',    16 }
}

local m = {
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
    end
}

if ssl ~= nil then
    local hexres = ffi.new('char[129]')

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
            
            for i = 0, hsize - 1 do
                ffi.C.snprintf(hexres + i * 2, 3, "%02x",
                                ffi.cast('unsigned int', r[i]))
            end
            return ffi.string(hexres, hsize * 2)
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
