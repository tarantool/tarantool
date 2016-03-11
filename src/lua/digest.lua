-- digest.lua (internal file)

local ffi = require 'ffi'
local crypto = require('crypto')

ffi.cdef[[
    /* internal implementation */
    unsigned char *SHA1internal(const unsigned char *d, size_t n, unsigned char *md);

    /* from libc */
    int snprintf(char *str, size_t size, const char *format, ...);

    typedef uint32_t (*crc32_func)(uint32_t crc,
        const unsigned char *buf, unsigned int len);
    extern int32_t guava(int64_t state, int32_t buckets);
    extern crc32_func crc32_calc;

    /* base64 */
    int base64_bufsize(int binsize);
    int base64_decode(const char *in_base64, int in_len, char *out_bin, int out_len);
    int base64_encode(const char *in_bin, int in_len, char *out_base64, int out_len);

    /* random */
    void random_bytes(char *, size_t);

    /* from third_party/PMurHash.h */
    void PMurHash32_Process(uint32_t *ph1, uint32_t *pcarry, const void *key, int len);
    uint32_t PMurHash32_Result(uint32_t h1, uint32_t carry, uint32_t total_length);
    uint32_t PMurHash32(uint32_t seed, const void *key, int len);
]]

local digest_shortcuts = {
    sha     = 'SHA',
    sha224  = 'SHA224',
    sha256  = 'SHA256',
    sha384  = 'SHA384',
    sha512  = 'SHA512',
    md5     = 'MD5',
    md4     = 'MD4',
}

local hexres = ffi.new('char[129]')

local function str_to_hex(r)
    for i = 0, r:len() - 1 do
        ffi.C.snprintf(hexres + i * 2, 3, "%02x",
            ffi.cast('unsigned char', r:byte(i + 1)))
    end
    return ffi.string(hexres, r:len() * 2)
end

local PMurHash
local PMurHash_methods = {

    update = function(self, str)
        str = tostring(str or '')
        ffi.C.PMurHash32_Process(self.seed, self.value, str, string.len(str))
        self.total_length = self.total_length + string.len(str)
    end,

    result = function(self)
        return ffi.C.PMurHash32_Result(self.seed[0], self.value[0], self.total_length)
    end,

    clear = function(self)
        self.seed[0] = self.default_seed
        self.total_length = 0
        self.value[0] = 0
    end,

    copy = function(self)
        local new_self = PMurHash.new()
        new_self.seed[0] = self.seed[0]
        new_self.value[0] = self.value[0]
        new_self.total_length = self.total_length
        return new_self
    end
}

PMurHash = {
    default_seed = 13,

    new = function(opts)
        opts = opts or {}
        local self = setmetatable({}, { __index = PMurHash_methods })
        self.default_seed = (opts.seed or PMurHash.default_seed)
        self.seed = ffi.new("int[1]", self.default_seed)
        self.value = ffi.new("int[1]", 0)
        self.total_length = 0
        return self
    end
}

setmetatable(PMurHash, {
    __call = function(self, str)
        str = tostring(str or '')
        return ffi.C.PMurHash32(PMurHash.default_seed, str, string.len(str))
    end
})

local CRC32
local CRC32_methods = {
    update = function(self, str)
        str = tostring(str or '')
        self.value = ffi.C.crc32_calc(self.value, str, string.len(str))
    end,

    result = function(self)
        return self.value
    end,

    clear = function(self)
        self.value = CRC32.crc_begin
    end,

    copy = function(self)
        local new_self = CRC32.new()
        new_self.value = self.value
        return new_self
    end
}

CRC32 = {
    crc_begin = 4294967295,

    new = function()
        local self = setmetatable({}, { __index = CRC32_methods })
        self.value = CRC32.crc_begin
        return self
    end
}

setmetatable(CRC32, {
    __call = function(self, str)
        str = tostring(str or '')
        return ffi.C.crc32_calc(CRC32.crc_begin, str, string.len(str))
    end
})

local m = {
    base64_encode = function(bin)
        if type(bin) ~= 'string' then
            error('Usage: digest.base64_encode(string)')
        end
        local blen = #bin
        local slen = ffi.C.base64_bufsize(blen)
        local str  = ffi.new('char[?]', slen)
        local len = ffi.C.base64_encode(bin, blen, str, slen)
        return ffi.string(str, len)
    end,

    base64_decode = function(str)
        if type(str) ~= 'string' then
            error('Usage: digest.base64_decode(string)')
        end
        local slen = #str
        local blen = math.ceil(slen * 3 / 4)
        local bin  = ffi.new('char[?]', blen)
        local len = ffi.C.base64_decode(str, slen, bin, blen)
        return ffi.string(bin, len)
    end,

    crc32 = CRC32,

    crc32_update = function(crc, str)
        str = tostring(str or '')
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
        return str_to_hex(ffi.string(r, 20))
    end,

    guava = function(state, buckets)
       return ffi.C.guava(state, buckets)
    end,

    urandom = function(n)
        if n == nil then
            error('Usage: digest.urandom(len)')
        end
        local buf = ffi.new('char[?]', n)
        ffi.C.random_bytes(buf, n)
        return ffi.string(buf, n)
    end,

    murmur = PMurHash
}

for digest, name in pairs(digest_shortcuts) do
    m[digest] = function (str)
        return crypto.digest[digest](str)
    end
    m[digest .. '_hex'] = function (str)
        return str_to_hex(crypto.digest[digest](str))
    end
end

m['aes256cbc'] = {
    encrypt = function (str, key, iv)
        return crypto.cipher.aes256.cbc.encrypt(str, key, iv)
    end,
    decrypt = function (str, key, iv)
        return crypto.cipher.aes256.cbc.decrypt(str, key, iv)
    end
}

return m
