-- crypto.lua (internal file)

---@class crypto.digest
---@field init fun(self: crypto.digest) Initialize the digest context.
---@field update fun(self: crypto.digest, input: string) Feed data.
---@field result fun(self: crypto.digest): string Return the digest value.
---@field free fun(self: crypto.digest) Free the digest context.

---@class crypto.hmac
---@field init fun(self: crypto.hmac, key: string) Initialize the HMAC.
---@field update fun(self: crypto.hmac, input: string) Feed data.
---@field result fun(self: crypto.hmac): string Return the HMAC value.
---@field free fun(self: crypto.hmac) Free the HMAC context.

---@class crypto.cipher
---@field init fun(self: crypto.cipher, key?: string, iv?: string) Initialize.
---@field update fun(self: crypto.cipher, input: string): string Process data.
---@field result fun(self: crypto.cipher): string Return final chunk.
---@field free fun(self: crypto.cipher) Free the cipher context.

---@class crypto.digest_api
---@overload fun(str: string): string
---@field new fun(): crypto.digest Create a new incremental digest object.

---@class crypto.hmac_api
---@overload fun(key: string, str: string): string
---@field new fun(key: string): crypto.hmac Create a new incremental HMAC
---    object.

---@class crypto.cipher_api
---@overload fun(str: string, key: string, iv: string): string
---@field new fun(key?: string, iv?: string): crypto.cipher Create a new
---    incremental cipher object.

---@class crypto.digest_module
---@field dss crypto.digest_api DSS digest.
---@field dss1 crypto.digest_api DSS-1 digest.
---@field md2 crypto.digest_api MD2 digest.
---@field md4 crypto.digest_api MD4 digest.
---@field md5 crypto.digest_api MD5 digest.
---@field mdc2 crypto.digest_api MDC2 digest.
---@field ripemd160 crypto.digest_api RIPEMD-160 digest.
---@field sha1 crypto.digest_api SHA-1 digest.
---@field sha224 crypto.digest_api SHA-224 digest.
---@field sha256 crypto.digest_api SHA-256 digest.
---@field sha384 crypto.digest_api SHA-384 digest.
---@field sha512 crypto.digest_api SHA-512 digest.

---@class crypto.hmac_module
---@field md4 crypto.hmac_api HMAC-MD4.
---@field md5 crypto.hmac_api HMAC-MD5.
---@field ripemd160 crypto.hmac_api HMAC-RIPEMD-160.
---@field sha1 crypto.hmac_api HMAC-SHA-1.
---@field sha224 crypto.hmac_api HMAC-SHA-224.
---@field sha256 crypto.hmac_api HMAC-SHA-256.
---@field sha384 crypto.hmac_api HMAC-SHA-384.
---@field sha512 crypto.hmac_api HMAC-SHA-512.
---@field md4_hex fun(key: string, str: string): string Hex-encoded HMAC-MD4.
---@field md5_hex fun(key: string, str: string): string Hex-encoded HMAC-MD5.
---@field ripemd160_hex fun(key: string, str: string): string Hex-encoded
---    HMAC-RIPEMD-160.
---@field sha1_hex fun(key: string, str: string): string Hex-encoded
---    HMAC-SHA-1.
---@field sha224_hex fun(key: string, str: string): string Hex-encoded
---    HMAC-SHA-224.
---@field sha256_hex fun(key: string, str: string): string Hex-encoded
---    HMAC-SHA-256.
---@field sha384_hex fun(key: string, str: string): string Hex-encoded
---    HMAC-SHA-384.
---@field sha512_hex fun(key: string, str: string): string Hex-encoded
---    HMAC-SHA-512.

---@class crypto.cipher_mode
---@field encrypt crypto.cipher_api Encrypt a string.
---@field decrypt crypto.cipher_api Decrypt a string.

---@class crypto.cipher_algo
---@field cbc crypto.cipher_mode Cipher Block Chaining mode.
---@field cfb crypto.cipher_mode Cipher Feedback mode.
---@field ecb crypto.cipher_mode Electronic Codebook mode.
---@field ofb crypto.cipher_mode Output Feedback mode.

---@class crypto.cipher_module
---@field none crypto.cipher_algo None.
---@field aes128 crypto.cipher_algo AES-128.
---@field aes192 crypto.cipher_algo AES-192.
---@field aes256 crypto.cipher_algo AES-256.
---@field des crypto.cipher_algo DES.

---@class crypto_module
---@field cipher_algo table<string, integer> Cipher algorithm identifiers.
---@field cipher_mode table<string, integer> Cipher mode identifiers.
---@field digest crypto.digest_module Digest algorithms.
---@field hmac crypto.hmac_module HMAC algorithms.
---@field cipher crypto.cipher_module Cipher algorithms.

local ffi = require('ffi')
local buffer = require('buffer')
local cord_ibuf_take = buffer.internal.cord_ibuf_take
local cord_ibuf_put = buffer.internal.cord_ibuf_put

ffi.cdef[[
    /* from openssl/err.h */
    unsigned long crypto_ERR_get_error(void);
    char *crypto_ERR_error_string(unsigned long e, char *buf);

    /* from openssl/evp.h */
    typedef void ENGINE;

    typedef struct {} EVP_MD_CTX;
    typedef struct {} EVP_MD;
    EVP_MD_CTX *crypto_EVP_MD_CTX_new(void);
    void crypto_EVP_MD_CTX_free(EVP_MD_CTX *ctx);
    int crypto_EVP_DigestInit_ex(EVP_MD_CTX *ctx, const EVP_MD *type,
                                 ENGINE *impl);
    int crypto_EVP_DigestUpdate(EVP_MD_CTX *ctx, const void *d, size_t cnt);
    int crypto_EVP_DigestFinal_ex(EVP_MD_CTX *ctx, unsigned char *md,
                                  unsigned int *s);
    const EVP_MD *crypto_EVP_get_digestbyname(const char *name);

    typedef struct {} HMAC_CTX;
    HMAC_CTX *crypto_HMAC_CTX_new(void);
    void crypto_HMAC_CTX_free(HMAC_CTX *ctx);
    int crypto_HMAC_Init_ex(HMAC_CTX *ctx, const void *key, int len,
                            const char *digest, const EVP_MD *md, ENGINE *impl);
    int crypto_HMAC_Update(HMAC_CTX *ctx, const unsigned char *data,
                           size_t len);
    int crypto_HMAC_Final(HMAC_CTX *ctx, unsigned char *md,
                          unsigned int *len, unsigned int size);

    enum crypto_algo {
        CRYPTO_ALGO_NONE,
        CRYPTO_ALGO_AES128,
        CRYPTO_ALGO_AES192,
        CRYPTO_ALGO_AES256,
        CRYPTO_ALGO_DES,
    };

    enum crypto_mode {
        CRYPTO_MODE_ECB,
        CRYPTO_MODE_CBC,
        CRYPTO_MODE_CFB,
        CRYPTO_MODE_OFB,
    };

    enum crypto_direction {
        CRYPTO_DIR_DECRYPT = 0,
        CRYPTO_DIR_ENCRYPT = 1,
    };

    struct crypto_stream;

    struct crypto_stream *
    crypto_stream_new(enum crypto_algo algo, enum crypto_mode mode,
                      enum crypto_direction dir);

    int
    crypto_stream_begin(struct crypto_stream *s, const char *key, int key_size,
                        const char *iv, int iv_size);

    int
    crypto_stream_append(struct crypto_stream *s, const char *in, int in_size,
                         char *out, int out_size);

    int
    crypto_stream_commit(struct crypto_stream *s, char *out, int out_size);

    void
    crypto_stream_delete(struct crypto_stream *s);
]]

local function openssl_err_str()
  return ffi.string(ffi.C.crypto_ERR_error_string(ffi.C.crypto_ERR_get_error(), nil))
end

local digests = {}
for class, _ in pairs({
    md2 = 'MD2', md4 = 'MD4', md5 = 'MD5',
    sha1 = 'SHA1', sha224 = 'SHA224',
    sha256 = 'SHA256', sha384 = 'SHA384', sha512 = 'SHA512',
    dss = 'DSS', dss1 = 'DSS1', mdc2 = 'MDC2', ripemd160 = 'RIPEMD160'}) do
    local digest = ffi.C.crypto_EVP_get_digestbyname(class)
    if digest ~= nil then
        digests[class] = digest
    end
end

local digest_mt = {}

local function digest_gc(ctx)
    ffi.C.crypto_EVP_MD_CTX_free(ctx)
end

---Create a new incremental digest object.
---
---@param digest ffi.cdata* the digest algorithm (EVP_MD pointer).
---@return crypto.digest self the incremental digest object.
local function digest_new(digest)
    local ctx = ffi.C.crypto_EVP_MD_CTX_new()
    if ctx == nil then
        error('Can\'t create digest ctx: ' .. openssl_err_str())
    end
    ffi.gc(ctx, digest_gc)
    local self = setmetatable({
        ctx = ctx,
        digest = digest,
        buf = buffer.ibuf(64),
        initialized = false,
    }, digest_mt)
    self:init()
    return self
end

---Initialize the digest context.
---
---@param self crypto.digest
local function digest_init(self)
    if self.ctx == nil then
        error('Digest context isn\'t usable')
    end
    if ffi.C.crypto_EVP_DigestInit_ex(self.ctx, self.digest, nil) ~= 1 then
        error('Can\'t init digest: ' .. openssl_err_str())
    end
    self.initialized = true
end

---Feed data into the digest.
---
---@param self crypto.digest
---@param input string the data to digest.
local function digest_update(self, input)
    if not self.initialized then
        error('Digest not initialized')
    end
    if ffi.C.crypto_EVP_DigestUpdate(self.ctx, input, input:len()) ~= 1 then
        error('Can\'t update digest: ' .. openssl_err_str())
    end
end

---Return the resulting digest.
---
---@param self crypto.digest
---@return string result the digest value.
local function digest_final(self)
    if not self.initialized then
        error('Digest not initialized')
    end
    self.initialized = false
    local ai = ffi.new('int[1]')
    if ffi.C.crypto_EVP_DigestFinal_ex(self.ctx, self.buf.wpos, ai) ~= 1 then
        error('Can\'t finalize digest: ' .. openssl_err_str())
    end
    return ffi.string(self.buf.wpos, ai[0])
end

---Free the digest context.
---
---@param self crypto.digest
local function digest_free(self)
    ffi.C.crypto_EVP_MD_CTX_free(self.ctx)
    ffi.gc(self.ctx, nil)
    self.ctx = nil
    self.initialized = false
end

digest_mt = {
    __index = {
          init = digest_init,
          update = digest_update,
          result = digest_final,
          free = digest_free
    }
}

local hmacs = digests

local hmac_mt = {}

local function hmac_gc(ctx)
    ffi.C.crypto_HMAC_CTX_free(ctx)
end

---Create a new incremental HMAC object.
---
---@param class string the HMAC algorithm name.
---@param digest ffi.cdata* the digest algorithm (EVP_MD pointer).
---@param key string the HMAC key.
---@return crypto.hmac self the incremental HMAC object.
local function hmac_new(class, digest, key)
    if key == nil then
        error('Key should be specified for HMAC operations')
    end
    local ctx = ffi.C.crypto_HMAC_CTX_new()
    if ctx == nil then
        error('Can\'t create HMAC ctx: ' .. openssl_err_str())
    end
    ffi.gc(ctx, hmac_gc)
    local self = setmetatable({
        ctx = ctx,
        class = class,
        digest = digest,
        initialized = false,
    }, hmac_mt)
    self:init(key)
    return self
end

---Initialize the HMAC context with a key.
---
---@param self crypto.hmac
---@param key string the HMAC key.
local function hmac_init(self, key)
    if self.ctx == nil then
        error('HMAC context isn\'t usable')
    end
    if ffi.C.crypto_HMAC_Init_ex(self.ctx, key, key:len(), self.class,
                                 self.digest, nil) ~= 1 then
        error('Can\'t init HMAC: ' .. openssl_err_str())
    end
    self.initialized = true
end

---Feed data into the HMAC.
---
---@param self crypto.hmac
---@param input string the data to process.
local function hmac_update(self, input)
    if not self.initialized then
        error('HMAC not initialized')
    end
    if ffi.C.crypto_HMAC_Update(self.ctx, input, input:len()) ~= 1 then
        error('Can\'t update HMAC: ' .. openssl_err_str())
    end
end

---Return the resulting HMAC.
---
---@param self crypto.hmac
---@return string result the HMAC value.
local function hmac_final(self)
    if not self.initialized then
        error('HMAC not initialized')
    end
    self.initialized = false
    local ibuf = cord_ibuf_take()
    local buf_size = 64
    local buf = ibuf:alloc(buf_size)
    local ai = ffi.new('int[1]')
    if ffi.C.crypto_HMAC_Final(self.ctx, buf, ai, buf_size) ~= 1 then
        cord_ibuf_put(ibuf)
        error('Can\'t finalize HMAC: ' .. openssl_err_str())
    end
    local result = ffi.string(buf, ai[0])
    cord_ibuf_put(ibuf)
    return result
end

---Free the HMAC context.
---
---@param self crypto.hmac
local function hmac_free(self)
    ffi.C.crypto_HMAC_CTX_free(self.ctx)
    ffi.gc(self.ctx, nil)
    self.ctx = nil
    self.initialized = false
end

hmac_mt = {
    __index = {
          init = hmac_init,
          update = hmac_update,
          result = hmac_final,
          free = hmac_free
    }
}

local crypto_stream_mt = {}

local function crypto_stream_gc(ctx)
    ffi.C.crypto_stream_delete(ctx)
end

---Create a new incremental cipher object.
---
---@param algo integer the cipher algorithm.
---@param mode integer the cipher mode.
---@param key string the encryption/decryption key.
---@param iv string the initialization vector.
---@param direction integer encrypt or decrypt direction.
---@return crypto.cipher self the incremental cipher object.
local function crypto_stream_new(algo, mode, key, iv, direction)
    local ctx = ffi.C.crypto_stream_new(algo, mode, direction)
    if ctx == nil then
        box.error()
    end
    ffi.gc(ctx, crypto_stream_gc)
    local self = setmetatable({
        ctx = ctx,
        buf = buffer.ibuf(),
        is_initialized = false,
    }, crypto_stream_mt)
    self:init(key, iv)
    return self
end

---Initialize the cipher context with a key and an initialization vector.
---
---@param self crypto.cipher
---@param key string the encryption/decryption key.
---@param iv string the initialization vector.
local function crypto_stream_begin(self, key, iv)
    local ctx = self.ctx
    if not ctx then
        error('Cipher context isn\'t usable')
    end
    self.key = key or self.key
    self.iv = iv or self.iv
    if self.key and self.iv then
        if ffi.C.crypto_stream_begin(ctx, self.key, self.key:len(),
                                     self.iv, self.iv:len()) ~= 0 then
            box.error()
        end
        self.is_initialized = true
    end
end

---Feed data into the cipher.
---
---@param self crypto.cipher
---@param input string the data to process.
---@return string result the processed data.
local function crypto_stream_append(self, input)
    if not self.is_initialized then
        ---@diagnostic disable-next-line: return-type-mismatch
        return error('Cipher not initialized')
    end
    if type(input) ~= 'string' then
        error("Usage: cipher:update(string)")
    end
    local append = ffi.C.crypto_stream_append
    local out_size = append(self.ctx, input, input:len(), nil, 0)
    local wpos = self.buf:reserve(out_size)
    out_size = append(self.ctx, input, input:len(), wpos, out_size)
    if out_size < 0 then
        box.error()
    end
    return ffi.string(wpos, out_size)
end

---Return the final chunk of the cipher result.
---
---@param self crypto.cipher
---@return string result the final processed data.
local function crypto_stream_commit(self)
    if not self.is_initialized then
        ---@diagnostic disable-next-line: return-type-mismatch
        return error('Cipher not initialized')
    end
    local commit = ffi.C.crypto_stream_commit
    local out_size = commit(self.ctx, nil, 0)
    local wpos = self.buf:reserve(out_size)
    out_size = commit(self.ctx, wpos, out_size)
    if out_size < 0 then
        box.error()
    end
    self.is_initialized = false
    return ffi.string(wpos, out_size)
end

---Free the cipher context.
---
---@param self crypto.cipher
local function crypto_stream_free(self)
    crypto_stream_gc(ffi.gc(self.ctx, nil))
    self.ctx = nil
    self.key = nil
    self.iv = nil
    self.is_initialized = false
end

crypto_stream_mt = {
    __index = {
          init = crypto_stream_begin,
          update = crypto_stream_append,
          result = crypto_stream_commit,
          free = crypto_stream_free
    }
}

---@type crypto.digest_module
---@diagnostic disable-next-line: missing-fields
local digest_api = {}
for class, digest in pairs(digests) do
    digest_api[class] = setmetatable({
        new = function () return digest_new(digest) end
    }, {
        __call = function (self, str)
            if type(str) ~= 'string' then
                error("Usage: digest."..class.."(string)")
            end
            local ctx = digest_new(digest)
            ctx:update(str)
            local res = ctx:result()
            ctx:free()
            return res
        end
    })
end

digest_api = setmetatable(digest_api,
    {__index = function(self, digest)
        return error('Digest method "' .. digest .. '" is not supported')
    end })

---@type crypto.hmac_module
---@diagnostic disable-next-line: missing-fields
local hmac_api = {}
for class, digest in pairs(hmacs) do
    hmac_api[class] = setmetatable({
        new = function (key) return hmac_new(class, digest, key) end
    }, {
        __call = function (self, key, str)
            if type(str) ~= 'string' then
                error("Usage: hmac."..class.."(key, string)")
            end
            local ctx = hmac_new(class, digest, key)
            ctx:update(str)
            local res = ctx:result()
            ctx:free()
            return res
        end
    })
    hmac_api[class .. '_hex'] = function (key, str)
        if type(str) ~= 'string' then
            error("Usage: hmac."..class.."_hex(key, string)")
        end
        return string.hex(hmac_api[class](key, str))
    end
end

hmac_api = setmetatable(hmac_api,
    {__index = function(self, digest)
        return error('HMAC method "' .. digest .. '" is not supported')
    end })

---@type table<string, integer>
local crypto_algos = {
    none = ffi.C.CRYPTO_ALGO_NONE,
    aes128 = ffi.C.CRYPTO_ALGO_AES128,
    aes192 = ffi.C.CRYPTO_ALGO_AES192,
    aes256 = ffi.C.CRYPTO_ALGO_AES256,
    des = ffi.C.CRYPTO_ALGO_DES
}
---@type table<string, integer>
local crypto_modes = {
    ecb = ffi.C.CRYPTO_MODE_ECB,
    cbc = ffi.C.CRYPTO_MODE_CBC,
    cfb = ffi.C.CRYPTO_MODE_CFB,
    ofb = ffi.C.CRYPTO_MODE_OFB
}
local crypto_dirs = {
    encrypt = ffi.C.CRYPTO_DIR_ENCRYPT,
    decrypt = ffi.C.CRYPTO_DIR_DECRYPT
}

local algo_api_mt = {
    __index = function(self, mode)
        error('Cipher mode ' .. mode .. ' is not supported')
    end
}
local crypto_api_mt = {
    __index = function(self, cipher)
        return error('Cipher method "' .. cipher .. '" is not supported')
    end
}

---@type crypto.cipher_module
---@diagnostic disable-next-line: missing-fields
local crypto_api = {}
setmetatable(crypto_api, crypto_api_mt)
for algo_name, algo_value in pairs(crypto_algos) do
    local algo_api = setmetatable({}, algo_api_mt)
    crypto_api[algo_name] = algo_api
    for mode_name, mode_value in pairs(crypto_modes) do
        local mode_api = {}
        algo_api[mode_name] = mode_api
        for dir_name, dir_value in pairs(crypto_dirs) do
            mode_api[dir_name] = setmetatable({
                new = function(key, iv)
                    return crypto_stream_new(algo_value, mode_value, key, iv,
                                             dir_value)
                end
            }, {
                __call = function(self, str, key, iv)
                    local ctx = crypto_stream_new(algo_value, mode_value, key,
                                                  iv, dir_value)
                    local res = ctx:update(str)
                    res = res .. ctx:result()
                    ctx:free()
                    return res
                end
            })
        end
    end
end

local public_methods = {
    digest = digest_api,
    hmac   = hmac_api,
    cipher = crypto_api,
}

local module_mt = {
    __serialize = function(self)
        return public_methods
    end,
    __index = public_methods
}

---The `crypto` module is used to compute a digest value from a string
---(usually with a cryptographic hash function), to produce HMAC message
---authentication codes, and to encrypt or decrypt strings. Tarantool's
---`crypto` module supports AES, DES, DSS, MD4, MD5, MDC2, RIPEMD, SHA-1
---and SHA-2 algorithms. Some of the crypto functionality is also present
---in the `digest` module.
---
---**Digest example:**
---
---```lua
---crypto.digest.md4('string')
---crypto.digest.sha512('string')
---```
---
---**HMAC example:**
---
---```lua
---crypto.hmac.md4('key', 'string')
---crypto.hmac.md4_hex('key', 'string')
---```
---
---**Cipher example:**
---
---```lua
---_16byte_iv = '1234567890123456'
---_16byte_pass = '1234567890123456'
---e = crypto.cipher.aes128.cbc.encrypt('string', _16byte_pass,
---                                      _16byte_iv)
---crypto.cipher.aes128.cbc.decrypt(e, _16byte_pass, _16byte_iv)
---```
---
---**Incremental methods:**
---
---Suppose that a digest is computed for a string 'A', then a new part 'B'
---is appended, and a new digest is required. The new digest could be
---recomputed for the whole string 'AB', but it is faster to reuse the
---state computed for 'A' and apply changes based on 'B'. This is called
---multi-step or "incremental" digesting, which Tarantool supports for all
---crypto functions.
---
---```lua
---crypto = require('crypto')
---
---- print aes-192 digest of 'AB', with one step, then incrementally
---key = 'key/key/key/key/key/key/'
---iv =  'iviviviviviviviv'
---print(crypto.cipher.aes192.cbc.encrypt('AB', key, iv))
---c = crypto.cipher.aes192.cbc.encrypt.new(key)
---c:init(nil, iv)
---c:update('A')
---c:update('B')
---print(c:result())
---c:free()
---
---- print sha-256 digest of 'AB', with one step, then incrementally
---print(crypto.digest.sha256('AB'))
---c = crypto.digest.sha256.new()
---c:init()
---c:update('A')
---c:update('B')
---print(c:result())
---c:free()
---```
---
---**Getting the same results from the digest and crypto modules:**
---
---The following functions are equivalent. For example, the `digest`
---function and the `crypto` function produce the same result.
---
---```lua
---crypto.cipher.aes256.cbc.encrypt('x', b32, b16) ==
---    digest.aes256cbc.encrypt('x', b32, b16)
---crypto.digest.md4('string') == digest.md4('string')
---crypto.digest.md5('string') == digest.md5('string')
---crypto.digest.sha1('string') == digest.sha1('string')
---crypto.digest.sha224('string') == digest.sha224('string')
---crypto.digest.sha256('string') == digest.sha256('string')
---crypto.digest.sha384('string') == digest.sha384('string')
---crypto.digest.sha512('string') == digest.sha512('string')
---```
---
---The set of available digest and HMAC algorithms depends on the
---OpenSSL build Tarantool is linked with.
local crypto = {
    ---Cipher algorithm identifiers.
    cipher_algo = crypto_algos,
    ---Cipher mode identifiers.
    cipher_mode = crypto_modes,
    ---Digest algorithms.
    digest = digest_api,
    ---HMAC algorithms.
    hmac = hmac_api,
    ---Cipher algorithms.
    cipher = crypto_api,
}
setmetatable(crypto, module_mt)

return crypto
