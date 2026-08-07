-- digest.lua (internal file)

local ffi = require('ffi')
local crypto = require('crypto')
local bit = require('bit')
local buffer = require('buffer')
local utils = require('internal.utils')
local cord_ibuf_take = buffer.internal.cord_ibuf_take
local cord_ibuf_put = buffer.internal.cord_ibuf_put
local check_param = utils.check_param

ffi.cdef[[
    /* from libc */
    int snprintf(char *str, size_t size, const char *format, ...);

    typedef uint32_t (*crc32_func)(uint32_t crc,
        const unsigned char *buf, unsigned int len);
    extern int32_t guava(uint64_t state, int32_t buckets);
    extern crc32_func crc32_calc;

    /* base64 */
    int base64_decode_bufsize(int base64_len);
    int base64_decode(const char *in_base64, int in_len, char *out_bin, int out_len);
    int base64_encode_bufsize(int bin_len, int options);
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

---A "digest" is a value which is returned by a function (usually a
---cryptographic hash function), applied against a string. Tarantool's
---`digest` module supports several types of cryptographic hash functions
---(AES, MD4, MD5, SHA-1, SHA-2, PBKDF2) as well as a checksum function
---(CRC32), two functions for base64, and two non-cryptographic hash
---functions (guava, murmur). Some of the digest functionality is also
---present in the `crypto` module.

---An unsigned 64-bit integer.
---@alias uint64_t integer

---@class digest.aes256cbc
---@field encrypt fun(str: string, key: string, iv: string): string
---@field decrypt fun(str: string, key: string, iv: string): string

---@class digest.crc32
---@operator call(string): integer
---@overload fun(str: string): integer
---@field new fun(): digest.crc32ctx Create a new object.

---@class digest.crc32ctx
---@field update fun(self: digest.crc32ctx, str: string) Feed data.
---@field result fun(self: digest.crc32ctx): integer Return the checksum.
---@field clear fun(self: digest.crc32ctx) Reset the state.
---@field copy fun(self: digest.crc32ctx): digest.crc32ctx Copy the state.

---@class digest.murmur
---@operator call(string): integer
---@overload fun(str: string): integer
---@field new fun(opts?: { seed?: integer }): digest.murmurctx Create a new
---    object.

---@class digest.murmurctx
---@field update fun(self: digest.murmurctx, str: string) Feed data.
---@field result fun(self: digest.murmurctx): integer Return the hash.
---@field clear fun(self: digest.murmurctx) Reset the state.
---@field copy fun(self: digest.murmurctx): digest.murmurctx Copy the state.

---@class digest.xxhash32
---@operator call(string, integer?): integer
---@overload fun(str: string, seed?: integer): integer
---@field new fun(seed?: integer): digest.xxhash32ctx Create a new object.

---@class digest.xxhash32ctx
---@field update fun(self: digest.xxhash32ctx, str: string) Feed data.
---@field result fun(self: digest.xxhash32ctx): integer Return the hash.
---@field clear fun(self: digest.xxhash32ctx, seed?: integer) Reset the state.
---@field copy fun(self: digest.xxhash32ctx): digest.xxhash32ctx Copy the
---    state.

---@class digest.xxhash64
---@operator call(string, integer?): uint64_t
---@overload fun(str: string, seed?: integer): uint64_t
---@field new fun(seed?: integer): digest.xxhash64ctx Create a new object.

---@class digest.xxhash64ctx
---@field update fun(self: digest.xxhash64ctx, str: string) Feed data.
---@field result fun(self: digest.xxhash64ctx): uint64_t Return the hash.
---@field clear fun(self: digest.xxhash64ctx, seed?: integer) Reset the state.
---@field copy fun(self: digest.xxhash64ctx): digest.xxhash64ctx Copy the
---    state.

---@class digest
---@field aes256cbc digest.aes256cbc
---Returns 128-bit binary string = digest made with MD4.
---@field md4 fun(str: string): string
---Returns 32-byte string = hexadecimal of a digest calculated with md4.
---@field md4_hex fun(str: string): string
---Returns 128-bit binary string = digest made with MD5.
---@field md5 fun(str: string): string
---Returns 32-byte string = hexadecimal of a digest calculated with md5.
---@field md5_hex fun(str: string): string
---Returns binary string = digest made with PBKDF2.
---@field pbkdf2 fun(pass: string, salt: string, iters?: integer,
---    digest_len?: integer): string
---Returns hexadecimal of a digest made with PBKDF2.
---@field pbkdf2_hex fun(pass: string, salt: string, iters?: integer,
---    digest_len?: integer): string
---Returns 160-bit binary string = digest made with SHA-1.
---@field sha1 fun(str: string): string
---Returns 40-byte string = hexadecimal of a digest calculated with sha1.
---@field sha1_hex fun(str: string): string
---Returns 224-bit binary string = digest made with SHA-2.
---@field sha224 fun(str: string): string
---Returns 56-byte string = hexadecimal of a digest calculated with sha224.
---@field sha224_hex fun(str: string): string
---Returns 256-bit binary string = digest made with SHA-2.
---@field sha256 fun(str: string): string
---Returns 64-byte string = hexadecimal of a digest calculated with sha256.
---@field sha256_hex fun(str: string): string
---Returns 384-bit binary string = digest made with SHA-2.
---@field sha384 fun(str: string): string
---Returns 96-byte string = hexadecimal of a digest calculated with sha384.
---@field sha384_hex fun(str: string): string
---Returns 512-bit binary string = digest made with SHA-2.
---@field sha512 fun(str: string): string
---Returns 128-byte string = hexadecimal of a digest calculated with sha512.
---@field sha512_hex fun(str: string): string
---Returns base64 encoding from a regular string.
---@field base64_encode fun(
---    bin: string,
---    options?: { nopad?: boolean, nowrap?: boolean, urlsafe?: boolean }
---): string
---Returns a regular string from a base64 encoding.
---@field base64_decode fun(str: string): string
---Returns an array of random bytes with length = integer.
---@field urandom fun(n: integer): string
---@field crc32 digest.crc32
---Returns a 32-bit checksum made with CRC32.
---@field crc32_update fun(crc: integer, str: string): integer
---Returns a number made with a consistent hash.
---@field guava fun(state: integer, buckets: integer): integer
---@field murmur digest.murmur
---@field xxhash32 digest.xxhash32
---@field xxhash64 digest.xxhash64

local builtin = ffi.C

-- @sa base64.h
local BASE64_NOPAD = 1
local BASE64_NOWRAP = 2
local BASE64_URLSAFE = 7

local internal = require("digest.lib")

---@type digest.murmur
---@diagnostic disable-next-line: missing-fields
local PMurHash = {}
local PMurHash_methods = {

    update = function(self, str)
        if type(str) ~= 'string' then
            box.error(box.error.ILLEGAL_PARAMS,
                      'Usage: murhash:update(string)', 2)
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

---@diagnostic disable-next-line: missing-fields
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
            box.error(box.error.ILLEGAL_PARAMS,
                      'Usage: digest.murhash(string)', 2)
        end
        return builtin.PMurHash32(PMurHash.default_seed, str, string.len(str))
    end
})

---@type digest.crc32
---@diagnostic disable-next-line: missing-fields
local CRC32 = {}
local CRC32_methods = {
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

---@diagnostic disable-next-line: missing-fields
CRC32 = {
    crc_begin = 4294967295,

    new = function()
        local self = setmetatable({}, { __index = CRC32_methods })
        self.value = CRC32.crc_begin
        return self
    end
}

local __crc32 = require('crc32.internal')(CRC32)

CRC32_methods.update = __crc32.update

setmetatable(CRC32, {
    __call = __crc32.__call
})

---Returns a binary string = digest made with PBKDF2.
---
---For effective encryption the `iters` value should be at least several
---thousand. The `digest_len` value determines the length of the resulting
---binary string.
---
---**Note:** `digest.pbkdf2()` yields and should not be used in a transaction
---(between `box.begin()` and `box.commit()`/`box.rollback()`). PBKDF2 is a
---time-consuming hash algorithm. It runs in a separate coio thread. While
---calculations are performed, the fiber that calls `digest.pbkdf2()` yields
---and another fiber continues working in the tx thread.
---
---@param pass string password.
---@param salt string salt.
---@param iters? integer number of iterations. Default is 100000.
---@param digest_len? integer length of the resulting string. Default is 128.
---@return string result the digest.
local function pbkdf2(pass, salt, iters, digest_len)
    if type(pass) ~= 'string' or type(salt) ~= 'string' then
        box.error(box.error.ILLEGAL_PARAMS,
                  'Usage: digest.pbkdf2(pass, salt[,iters][,digest_len])', 2)
    end
    if iters and type(iters) ~= 'number' then
        box.error(box.error.ILLEGAL_PARAMS, 'iters must be a number', 2)
    end
    if digest_len and type(digest_len) ~= 'number' then
        box.error(box.error.ILLEGAL_PARAMS, 'digest_len must be a number', 2)
    end
    iters = iters or 100000
    digest_len = digest_len or 128
    if digest_len > 128 then
        box.error(box.error.ILLEGAL_PARAMS, 'too big digest size', 2)
    end
    return internal.pbkdf2(pass, salt, iters, digest_len)
end

---Encode a string to Base64.
---
---The possible options are:
---
---* `nopad` -- result must not include '=' for padding at the end,
---* `nowrap` -- result must not include line feed for splitting lines
---  after 72 characters,
---* `urlsafe` -- result must not include '=' or line feed, and may
---  contain '-' or '_' instead of '+' or '/' for positions 62 and 63 in
---  the index table.
---
---Options may be `true` or `false`, the default value is `false`.
---
---**Example:**
---
--- ```lua
--- digest.base64_encode(string_variable, {nopad = true})
--- ```
---
---@param bin string the string to encode.
---@param options? { nopad?: boolean, nowrap?: boolean, urlsafe?: boolean }
---    encoding options.
---@return string result the base64 encoding.
local function base64_encode(bin, options)
    if type(bin) ~= 'string' or
       options ~= nil and type(options) ~= 'table' then
        box.error(box.error.ILLEGAL_PARAMS,
                  'Usage: digest.base64_encode(string[, table])', 2)
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
    local slen = builtin.base64_encode_bufsize(blen, mask)
    local ibuf = cord_ibuf_take()
    local str = ibuf:alloc(slen)
    local len = builtin.base64_encode(bin, blen, str, slen, mask)
    str = ffi.string(str, len)
    cord_ibuf_put(ibuf)
    return str
end

---Decode a Base64-encoded string.
---
---@param str string the base64 encoding.
---@return string result the decoded string.
local function base64_decode(str)
    if type(str) ~= 'string' then
        box.error(box.error.ILLEGAL_PARAMS,
                  'Usage: digest.base64_decode(string)', 2)
    end
    local slen = #str
    local blen = builtin.base64_decode_bufsize(slen);
    local ibuf = cord_ibuf_take()
    local bin = ibuf:alloc(blen)
    local len = builtin.base64_decode(str, slen, bin, blen)
    bin = ffi.string(bin, len)
    cord_ibuf_put(ibuf)
    return bin
end

---Update a CRC32 checksum incrementally.
---
---@param crc integer the current checksum value.
---@param str string the string to process.
---@return integer result the updated checksum.
local function crc32_update(crc, str)
    if type(str) ~= 'string' then
        box.error(box.error.ILLEGAL_PARAMS,
                  'Usage: digest.crc32_update(string)', 2)
    end
    return builtin.crc32_calc(tonumber(crc), str, string.len(str))
end

---Returns a number made with a consistent hash.
---
---The guava function uses the consistent hashing algorithm of the Google
---guava library. The first parameter should be a hash code; the second
---parameter should be the number of buckets; the returned value will be
---an integer between 0 and the number of buckets.
---
---**Example:**
---
--- ```tarantoolsession
--- tarantool> digest.guava(10863919174838991, 11)
--- ---
--- - 8
--- ...
--- ```
---
---@param state integer a hash code.
---@param buckets integer the number of buckets.
---@return integer result a number between 0 and the number of buckets.
local function guava(state, buckets)
    check_param(state, 'state', 'number', 2)
    check_param(buckets, 'buckets', 'number', 2)
    return builtin.guava(state, buckets)
end

---Get an array of random bytes.
---
---@param n integer length of the resulting array in bytes.
---@return string result an array of `n` random bytes.
local function urandom(n)
    if n == nil then
        box.error(box.error.ILLEGAL_PARAMS, 'Usage: digest.urandom(len)', 2)
    end
    local ibuf = cord_ibuf_take()
    local buf = ibuf:alloc(n)
    builtin.random_bytes(buf, n)
    buf = ffi.string(buf, n)
    cord_ibuf_put(ibuf)
    return buf
end

---Returns a hexadecimal string = digest made with PBKDF2.
---
---@param pass string password.
---@param salt string salt.
---@param iters? integer number of iterations. Default is 100000.
---@param digest_len? integer length of the resulting string. Default is 128.
---@return string result the hexadecimal digest.
local function pbkdf2_hex(pass, salt, iters, digest_len)
    if type(pass) ~= 'string' or type(salt) ~= 'string' then
        box.error(box.error.ILLEGAL_PARAMS,
                  'Usage: digest.pbkdf2_hex(pass, salt)', 2)
    end
    return string.hex(pbkdf2(pass, salt, iters, digest_len))
end

local function digest_call(name, str)
    return crypto.digest[name](str)
end

local function digest_hex(name, str)
    if type(str) ~= 'string' then
        box.error(box.error.ILLEGAL_PARAMS,
                  'Usage: digest.' .. name .. '_hex(string)', 2)
    end
    return string.hex(crypto.digest[name](str))
end

---Returns 128-bit binary string = digest made with MD4.
---
---@param str string
---@return string
local function md4(str)
    return digest_call('md4', str)
end

---Returns 32-byte string = hexadecimal of a digest calculated with md4.
---
---@param str string
---@return string
local function md4_hex(str)
    return digest_hex('md4', str)
end

---Returns 128-bit binary string = digest made with MD5.
---
---@param str string
---@return string
local function md5(str)
    return digest_call('md5', str)
end

---Returns 32-byte string = hexadecimal of a digest calculated with md5.
---
---@param str string
---@return string
local function md5_hex(str)
    return digest_hex('md5', str)
end

---Returns 160-bit binary string = digest made with SHA-1.
---
---@param str string
---@return string
local function sha1(str)
    return digest_call('sha1', str)
end

---Returns 40-byte string = hexadecimal of a digest calculated with sha1.
---
---@param str string
---@return string
local function sha1_hex(str)
    return digest_hex('sha1', str)
end

---Returns 224-bit binary string = digest made with SHA-2.
---
---@param str string
---@return string
local function sha224(str)
    return digest_call('sha224', str)
end

---Returns 56-byte string = hexadecimal of a digest calculated with sha224.
---
---@param str string
---@return string
local function sha224_hex(str)
    return digest_hex('sha224', str)
end

---Returns 256-bit binary string = digest made with SHA-2.
---
---@param str string
---@return string
local function sha256(str)
    return digest_call('sha256', str)
end

---Returns 64-byte string = hexadecimal of a digest calculated with sha256.
---
---@param str string
---@return string
local function sha256_hex(str)
    return digest_hex('sha256', str)
end

---Returns 384-bit binary string = digest made with SHA-2.
---
---@param str string
---@return string
local function sha384(str)
    return digest_call('sha384', str)
end

---Returns 96-byte string = hexadecimal of a digest calculated with sha384.
---
---@param str string
---@return string
local function sha384_hex(str)
    return digest_hex('sha384', str)
end

---Returns 512-bit binary string = digest made with SHA-2.
---
---@param str string
---@return string
local function sha512(str)
    return digest_call('sha512', str)
end

---Returns 128-byte string = hexadecimal of a digest calculated with sha512.
---
---@param str string
---@return string
local function sha512_hex(str)
    return digest_hex('sha512', str)
end

---Encrypt or decrypt a string using AES-256.
---@type digest.aes256cbc
local aes256cbc = {
    ---Encrypt a string using AES.
    ---
    ---@param str string the string to encrypt.
    ---@param key string the encryption key.
    ---@param iv string the initialization vector.
    ---@return string result 256-bit binary string = digest made with AES.
    encrypt = function(str, key, iv)
        return crypto.cipher.aes256.cbc.encrypt(str, key, iv)
    end,
    ---Decrypt a string using AES.
    ---
    ---@param str string the string to decrypt.
    ---@param key string the decryption key.
    ---@param iv string the initialization vector.
    ---@return string result 256-bit binary string = digest made with AES.
    decrypt = function(str, key, iv)
        return crypto.cipher.aes256.cbc.decrypt(str, key, iv)
    end,
}

local function make_xxhash(var)
    local xxHash

    local xxh_template = 'tnt_XXH%s_%s'
    local update_fn_name = string.format(xxh_template, var, 'update')
    local digest_fn_name = string.format(xxh_template, var, 'digest')
    local reset_fn_name = string.format(xxh_template, var, 'reset')
    local copy_fn_name = string.format(xxh_template, var, 'copyState')

    local function update(self, str)
        if type(str) ~= 'string' then
            local message = string.format("Usage: xxhash%s:update(string)", var)
            box.error(box.error.ILLEGAL_PARAMS, message, 2)
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
                local message = string.format(
                    'Usage: digest.xxhash%s(string[, unsigned number])', var)
                box.error(box.error.ILLEGAL_PARAMS, message, 2)
            end
            if seed == nil then
                seed = 0
            end
            return builtin[call_fn_name](str, #str, seed)
        end,
    })

    return xxHash
end

---@type digest.xxhash32
---@diagnostic disable-next-line: missing-fields
local xxhash32 = {} -- luacheck: ignore 311
xxhash32 = make_xxhash('32')

---@type digest.xxhash64
---@diagnostic disable-next-line: missing-fields
local xxhash64 = {} -- luacheck: ignore 311
xxhash64 = make_xxhash('64')

---The `digest` module computes a digest value from a string. It supports
---AES, MD4, MD5, SHA-1, SHA-2 and PBKDF2 hash functions, a CRC32
---checksum, two base64 functions, and two non-cryptographic hash
---functions (guava and murmur). Some of the digest functionality is also
---present in the `crypto` module.
---
---**Hashing example:**
---
---```lua
---digest = require('digest')
---digest.md4('string')
---digest.sha256('string')
---```
---
---**Base64 example:**
---
---```lua
---digest.base64_encode(string_variable, {nopad = true})
---```
---
---`digest.pbkdf2()` yields and should not be used in a transaction
---(between `box.begin()` and `box.commit()`/`box.rollback()`). PBKDF2 is
---a time-consuming hash algorithm. It runs in a separate coio thread.
---While calculations are performed, the fiber that calls `digest.pbkdf2()`
---yields and another fiber continues working in the tx thread.
---
---`digest.crc32()` and `digest.crc32_update()` use the CRC32 polynomial
---value `0x1EDC6F41` / `4812730177`. If compatibility with other
---programming languages is needed, ensure that they use the same
---polynomial.
---
---**Guava example:**
---
---```tarantoolsession
---tarantool> digest.guava(10863919174838991, 11)
----
---- 8
---...
---```
---
---**Murmur example:**
---
---```lua
---digest.murmur.new({seed = 0})
---```
---
---**Incremental methods:**
---
---Suppose that a digest is done for a string 'A', then a new part 'B' is
---appended, and a new digest is required. The new digest could be
---recomputed for the whole string 'AB', but it is faster to reuse the
---state computed for 'A' and apply changes based on the new part 'B'.
---This is called multi-step or "incremental" digesting, which Tarantool
---supports with crc32 and murmur.
---
---```lua
---digest = require('digest')
---
---- print crc32 of 'AB', with one step, then incrementally
---print(digest.crc32('AB'))
---c = digest.crc32.new()
---c:update('A')
---c:update('B')
---print(c:result())
---
---- print murmur hash of 'AB', with one step, then incrementally
---print(digest.murmur('AB'))
---m = digest.murmur.new()
---m:update('A')
---m:update('B')
---print(m:result())
---```
---
---**Example:**
---
---The following example creates two functions. `password_insert()`
---inserts a SHA-1 digest of a password into a tuple set, and
---`password_check()` checks an entered password.
---
---```tarantoolsession
---tarantool> digest = require('digest')
----
---...
---tarantool> function password_insert()
---         >   box.space.tester:insert{1234, digest.sha1('Secret Wordpass')}
---         >   return 'OK'
---         > end
----
---...
---tarantool> function password_check(password)
---         >   local t = box.space.tester:select{12345}
---         >   if digest.sha1(password) == t[2] then
---         >     return 'Password is valid'
---         >   else
---         >     return 'Password is not valid'
---         >   end
---         > end
----
---...
---```
---
local m = {
    ---Encode a string to Base64.
    base64_encode = base64_encode,
    ---Decode a Base64-encoded string.
    base64_decode = base64_decode,
    ---A 32-bit checksum made with CRC32.
    ---@type digest.crc32
    crc32 = CRC32,
    ---Update a CRC32 checksum incrementally.
    crc32_update = crc32_update,
    ---A number made with a consistent hash.
    guava = guava,
    ---An array of random bytes.
    urandom = urandom,
    ---MurmurHash.
    murmur = PMurHash,
    ---PBKDF2 digest.
    pbkdf2 = pbkdf2,
    ---Hexadecimal PBKDF2 digest.
    pbkdf2_hex = pbkdf2_hex,
    ---AES-256 digest.
    aes256cbc = aes256cbc,
    ---MD4 digest.
    md4 = md4,
    ---Hexadecimal MD4 digest.
    md4_hex = md4_hex,
    ---MD5 digest.
    md5 = md5,
    ---Hexadecimal MD5 digest.
    md5_hex = md5_hex,
    ---SHA-1 digest.
    sha1 = sha1,
    ---Hexadecimal SHA-1 digest.
    sha1_hex = sha1_hex,
    ---SHA-224 digest.
    sha224 = sha224,
    ---Hexadecimal SHA-224 digest.
    sha224_hex = sha224_hex,
    ---SHA-256 digest.
    sha256 = sha256,
    ---Hexadecimal SHA-256 digest.
    sha256_hex = sha256_hex,
    ---SHA-384 digest.
    sha384 = sha384,
    ---Hexadecimal SHA-384 digest.
    sha384_hex = sha384_hex,
    ---SHA-512 digest.
    sha512 = sha512,
    ---Hexadecimal SHA-512 digest.
    sha512_hex = sha512_hex,
    ---xxHash32.
    xxhash32 = xxhash32,
    ---xxHash64.
    xxhash64 = xxhash64,
}

return m
