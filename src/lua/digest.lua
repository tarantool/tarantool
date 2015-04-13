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
    extern int32_t guava(int64_t state, int32_t buckets);
    extern crc32_func crc32_calc;

    /* from third_party/PMurHash.h */
    void PMurHash32_Process(uint32_t *ph1, uint32_t *pcarry, const void *key, int len);
    uint32_t PMurHash32_Result(uint32_t h1, uint32_t carry, uint32_t total_length);
    uint32_t PMurHash32(uint32_t seed, const void *key, int len);
]]

local ssl
if ssl == nil then
    local variants = {
        'libssl.so.10',
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
    sha1    = { 'SHA1',   20 },
    sha224  = { 'SHA224', 28 },
    sha256  = { 'SHA256', 32 },
    sha384  = { 'SHA384', 48 },
    sha512  = { 'SHA512', 64 },
    md5     = { 'MD5',    16 },
    md4     = { 'MD4',    16 },
}

local hexres = ffi.new('char[129]')

local function tohex(r, size)
    for i = 0, size - 1 do
        ffi.C.snprintf(hexres + i * 2, 3, "%02x",
            ffi.cast('unsigned int', r[i]))
    end
    return ffi.string(hexres, size * 2)
end

local PMurHash = {
    default_seed = 13,

    update = function(self, str)
        str = tostring(str or '')
        ffi.C.PMurHash32_Process(self.seed, self.value, str, string.len(str))
        self.total_length = self.total_length + string.len(str)
    end,

    digest = function(self)
        return ffi.C.PMurHash32_Result(self.seed[0], self.value[0], self.total_length)
    end,

    clear = function(self)
        self.seed[0] = self.default_seed
        self.total_length = 0
        self.value[0] = 0
    end,
}

PMurHash.__call = function(self, str)
    str = tostring(str or '')
    return ffi.C.PMurHash32(PMurHash.default_seed, str, string.len(str))
end

PMurHash.copy = function(self)
    new_self = PMurHash.new{seed=self.default_seed}
    new_self.seed[0] = self.seed[0]
    new_self.value[0] = self.value[0]
    new_self.total_length = self.total_length
    return new_self
end

PMurHash.new = function(opts)
    opts = opts or {}
    local self = setmetatable({}, { __index = PMurHash })
    self.default_seed = (opts.seed or PMurHash.default_seed)
    self.seed = ffi.new("int[1]", self.default_seed)
    self.value = ffi.new("int[1]", 0)
    self.total_length = 0
    return self
end

local CRC32 = {
    crc_begin = 4294967295,

    update = function(self, str)
        str = tostring(str or '')
        self.value = ffi.C.crc32_calc(self.value, str, string.len(str))
    end,

    digest = function(self)
        return self.value
    end,

    clear = function(self)
        self.value = CRC32.crc_begin
    end,
}

CRC32.__call = function(self, str)
    str = tostring(str or '')
    return ffi.C.crc32_calc(CRC32.crc_begin, str, string.len(str))
end

CRC32.copy = function(self)
    new_self = CRC32.new()
    new_self.value = self.value
    return new_self
end

CRC32.new = function()
    local self = setmetatable({}, { __index == CRC32 })
    self.value = CRC32.crc_begin
    return self
end

local m = {
    crc32 = {
        new = CRC32.new,
    },

    crc32_update = function(crc, str)
        str = tostring(str or '')
        return ffi.C.crc32_calc(tonumber(crc), str, string.len(str))
    end,

    guava = function(state, buckets)
       return ffi.C.guava(state, buckets)
    end,

    murmur = {
       new = PMurHash.new,
    },
}

setmetatable(m.murmur, { __call = PMurHash.__call })
setmetatable(m.crc32, { __call = CRC32.__call })

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

box.digest = m

end
