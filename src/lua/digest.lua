-- digest.lua (internal file)

local ffi = require('ffi')
local crypto = require('crypto')
local bit = require('bit')
local buffer = require('buffer')
local cord_ibuf_take = buffer.internal.cord_ibuf_take
local cord_ibuf_put = buffer.internal.cord_ibuf_put

ffi.cdef[[
    /* internal implementation */
    unsigned char *SHA1internal(const unsigned char *d, size_t n, unsigned char *md);

    /* from libc */
    int snprintf(char *str, size_t size, const char *format, ...);

    typedef uint32_t (*crc32_func)(uint32_t crc,
        const unsigned char *buf, unsigned int len);
    extern int32_t guava(uint64_t state, int32_t buckets);
    extern crc32_func crc32_calc;

    /* base64 */
    int base64_bufsize(int binsize, int options);
    int base64_decode(const char *in_base64, int in_len, char *out_bin, int out_len);
    int base64_encode(const char *in_bin, int in_len, char *out_base64, int out_len, int options);

    /* random */
    void random_bytes(char *, size_t);

    /* from third_party/PMurHash.h */
    void PMurHash32_Process(uint32_t *ph1, uint32_t *pcarry, const void *key, int len);
    uint32_t PMurHash32_Result(uint32_t h1, uint32_t carry, uint32_t total_length);
    uint32_t PMurHash32(uint32_t seed, const void *key, int len);

    /* from third_party/zstd/lib/common/xxhash.c */
    typedef enum { XXH_OK=0, XXH_ERROR } XXH_errorcode;
    struct XXH32_state_s {
        unsigned total_len_32;
        unsigned large_len;
        unsigned v1;
        unsigned v2;
        unsigned v3;
        unsigned v4;
        unsigned mem32[4];   /* buffer defined as U32 for alignment */
        unsigned memsize;
        unsigned reserved;   /* never read nor write, will be removed in a future version */
    };

    struct XXH64_state_s {
        unsigned long long total_len;
        unsigned long long v1;
        unsigned long long v2;
        unsigned long long v3;
        unsigned long long v4;
        unsigned long long mem64[4];   /* buffer defined as U64 for alignment */
        unsigned memsize;
        unsigned reserved[2];          /* never read nor write, will be removed in a future version */
    };

    typedef unsigned int       XXH32_hash_t;
    typedef unsigned long long XXH64_hash_t;
    XXH32_hash_t tnt_XXH32 (const void* input, size_t length, unsigned int seed);
    XXH64_hash_t tnt_XXH64 (const void* input, size_t length, unsigned long long seed);

    typedef struct XXH32_state_s XXH32_state_t;
    typedef struct XXH64_state_s XXH64_state_t;

    XXH_errorcode tnt_XXH32_reset  (XXH32_state_t* statePtr, unsigned int seed);
    XXH_errorcode tnt_XXH32_update (XXH32_state_t* statePtr, const void* input, size_t length);
    XXH32_hash_t  tnt_XXH32_digest (const XXH32_state_t* statePtr);

    XXH_errorcode tnt_XXH64_reset  (XXH64_state_t* statePtr, unsigned long long seed);
    XXH_errorcode tnt_XXH64_update (XXH64_state_t* statePtr, const void* input, size_t length);
    XXH64_hash_t  tnt_XXH64_digest (const XXH64_state_t* statePtr);

    void tnt_XXH32_copyState(XXH32_state_t* restrict dst_state, const XXH32_state_t* restrict src_state);
    void tnt_XXH64_copyState(XXH64_state_t* restrict dst_state, const XXH64_state_t* restrict src_state);
]]

local builtin = ffi.C

-- @sa base64.h
local BASE64_NOPAD = 1
local BASE64_NOWRAP = 2
local BASE64_URLSAFE = 7

local digest_shortcuts = {
    sha224  = 'SHA224',
    sha256  = 'SHA256',
    sha384  = 'SHA384',
    sha512  = 'SHA512',
    md5     = 'MD5',
    md4     = 'MD4',
}
local internal = require("digest")

local PMurHash
local PMurHash_methods = {

    update = function(self, str)
        if type(str) ~= 'string' then
            error("Usage: murhash:update(string)")
        end
        builtin.PMurHash32_Process(self.seed, self.value, str, string.len(str))
        self.total_length = self.total_length + string.len(str)
    end,

    result = function(self)
        return builtin.PMurHash32_Result(self.seed[0], self.value[0], self.total_length)
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
        if type(str) ~= 'string' then
            error("Usage: digest.murhash(string)")
        end
        return builtin.PMurHash32(PMurHash.default_seed, str, string.len(str))
    end
})

local CRC32
local CRC32_methods = {
    update = function(self, str)
        if type(str) ~= 'string' then
            error("Usage crc32:update(string)")
        end
        self.value = builtin.crc32_calc(self.value, str, string.len(str))
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
        if type(str) ~= 'string' then
            error("Usage digest.crc32(string)")
        end
        return builtin.crc32_calc(CRC32.crc_begin, str, string.len(str))
    end
})

local pbkdf2 = function(pass, salt, iters, digest_len)
    if type(pass) ~= 'string' or type(salt) ~= 'string' then
        error("Usage: digest.pbkdf2(pass, salt[,iters][,digest_len])")
    end
    if iters and type(iters) ~= 'number' then
        error("iters must be a number")
    end
    if digest_len and type(digest_len) ~= 'number' then
        error("digest_len must be a number")
    end
    iters = iters or 100000
    digest_len = digest_len or 128
    if digest_len > 128 then
        error("too big digest size")
    end
    return internal.pbkdf2(pass, salt, iters, digest_len)
end

local m = {
    base64_encode = function(bin, options)
        if type(bin) ~= 'string' or
           options ~= nil and type(options) ~= 'table' then
            error('Usage: digest.base64_encode(string[, table])')
        end
        local mask = 0
        if options ~= nil then
            if options.urlsafe then
                mask = bit.bor(mask, BASE64_URLSAFE)
            end
            if options.nopad then
                mask = bit.bor(mask, BASE64_NOPAD)
            end
            if options.nowrap then
                mask = bit.bor(mask, BASE64_NOWRAP)
            end
        end
        local blen = #bin
        local slen = builtin.base64_bufsize(blen, mask)
        local ibuf = cord_ibuf_take()
        local str = ibuf:alloc(slen)
        local len = builtin.base64_encode(bin, blen, str, slen, mask)
        str = ffi.string(str, len)
        cord_ibuf_put(ibuf)
        return str
    end,

    base64_decode = function(str)
        if type(str) ~= 'string' then
            error('Usage: digest.base64_decode(string)')
        end
        local slen = #str
        local blen = math.ceil(slen * 3 / 4)
        local ibuf = cord_ibuf_take()
        local bin = ibuf:alloc(blen)
        local len = builtin.base64_decode(str, slen, bin, blen)
        bin = ffi.string(bin, len)
        cord_ibuf_put(ibuf)
        return bin
    end,

    crc32 = CRC32,

    crc32_update = function(crc, str)
        if type(str) ~= 'string' then
            error("Usage: digest.crc32_update(string)")
        end
        return builtin.crc32_calc(tonumber(crc), str, string.len(str))
    end,

    sha1 = function(str)
        if type(str) ~= 'string' then
            error("Usage: digest.sha1(string)")
        end
        local r = builtin.SHA1internal(str, #str, nil)
        return ffi.string(r, 20)
    end,

    sha1_hex = function(str)
        if type(str) ~= 'string' then
            error("Usage: digest.sha1_hex(string)")
        end
        local r = builtin.SHA1internal(str, #str, nil)
        return string.hex(ffi.string(r, 20))
    end,

    guava = function(state, buckets)
        return builtin.guava(state, buckets)
    end,

    urandom = function(n)
        if n == nil then
            error('Usage: digest.urandom(len)')
        end
        local ibuf = cord_ibuf_take()
        local buf = ibuf:alloc(n)
        builtin.random_bytes(buf, n)
        buf = ffi.string(buf, n)
        cord_ibuf_put(ibuf)
        return buf
    end,

    murmur = PMurHash,

    pbkdf2 = pbkdf2,

    pbkdf2_hex = function(pass, salt, iters, digest_len)
        if type(pass) ~= 'string' or type(salt) ~= 'string' then
            error("Usage: digest.pbkdf2_hex(pass, salt)")
        end
        return string.hex(pbkdf2(pass, salt, iters, digest_len))
    end
}

for digest, _ in pairs(digest_shortcuts) do
    m[digest] = function (str)
        return crypto.digest[digest](str)
    end
    m[digest .. '_hex'] = function (str)
        if type(str) ~= 'string' then
            error('Usage: digest.'..digest..'_hex(string)')
        end
        return string.hex(crypto.digest[digest](str))
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

for _, var in ipairs({'32', '64'}) do
    local xxHash

    local xxh_template = 'tnt_XXH%s_%s'
    local update_fn_name = string.format(xxh_template, var, 'update')
    local digest_fn_name = string.format(xxh_template, var, 'digest')
    local reset_fn_name = string.format(xxh_template, var, 'reset')
    local copy_fn_name = string.format(xxh_template, var, 'copyState')

    local function update(self, str)
        if type(str) ~= 'string' then
            local message = string.format("Usage xxhash%s:update(string)", var)
            error(message, 2)
        end
        builtin[update_fn_name](self.value, str, #str)
    end

    local function result(self)
        return builtin[digest_fn_name](self.value)
    end

    local function clear(self, seed)
        if seed == nil then
            seed = self.default_seed
        end
        builtin[reset_fn_name](self.value, seed)
    end

    local function copy(self)
        local copy = xxHash.new()
        builtin[copy_fn_name](copy.value, self.value)
        return copy
    end

    local state_type_name = string.format('XXH%s_state_t', var)
    local XXH_state_t = ffi.typeof(state_type_name)

    xxHash = {
        new = function(seed)
            local self = {
                update = update,
                result = result,
                clear = clear,
                copy = copy,
                value = ffi.new(XXH_state_t),
                default_seed = seed or 0,
            }
            self:clear(self.default_seed)
            return self
        end,
    }

    local call_fn_name = 'tnt_XXH' .. var
    setmetatable(xxHash, {
        __call = function(_, str, seed)
            if type(str) ~= 'string' then
                local message = string.format("Usage digest.xxhash%s(string[, unsigned number])", var)
                error(message, 2)
            end
            if seed == nil then
                seed = 0
            end
            return builtin[call_fn_name](str, #str, seed)
        end,
    })

    m['xxhash' .. var] = xxHash
end

return m
