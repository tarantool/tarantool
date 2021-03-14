-- crypto.lua (internal file)

local ffi = require('ffi')
local buffer = require('buffer')
local cord_ibuf_take = buffer.internal.cord_ibuf_take
local cord_ibuf_put = buffer.internal.cord_ibuf_put

ffi.cdef[[
    /* from openssl/err.h */
    unsigned long ERR_get_error(void);
    char *ERR_error_string(unsigned long e, char *buf);

    /* from openssl/evp.h */
    typedef void ENGINE;

    typedef struct {} EVP_MD_CTX;
    typedef struct {} EVP_MD;
    EVP_MD_CTX *crypto_EVP_MD_CTX_new(void);
    void crypto_EVP_MD_CTX_free(EVP_MD_CTX *ctx);
    int EVP_DigestInit_ex(EVP_MD_CTX *ctx, const EVP_MD *type, ENGINE *impl);
    int EVP_DigestUpdate(EVP_MD_CTX *ctx, const void *d, size_t cnt);
    int EVP_DigestFinal_ex(EVP_MD_CTX *ctx, unsigned char *md, unsigned int *s);
    const EVP_MD *EVP_get_digestbyname(const char *name);

    typedef struct {} HMAC_CTX;
    HMAC_CTX *crypto_HMAC_CTX_new(void);
    void crypto_HMAC_CTX_free(HMAC_CTX *ctx);
    int HMAC_Init_ex(HMAC_CTX *ctx, const void *key, int len,
                 const EVP_MD *md, ENGINE *impl);
    int HMAC_Update(HMAC_CTX *ctx, const unsigned char *data, size_t len);
    int HMAC_Final(HMAC_CTX *ctx, unsigned char *md, unsigned int *len);

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
  return ffi.string(ffi.C.ERR_error_string(ffi.C.ERR_get_error(), nil))
end

local digests = {}
for class, _ in pairs({
    md2 = 'MD2', md4 = 'MD4', md5 = 'MD5',
    sha1 = 'SHA1', sha224 = 'SHA224',
    sha256 = 'SHA256', sha384 = 'SHA384', sha512 = 'SHA512',
    dss = 'DSS', dss1 = 'DSS1', mdc2 = 'MDC2', ripemd160 = 'RIPEMD160'}) do
    local digest = ffi.C.EVP_get_digestbyname(class)
    if digest ~= nil then
        digests[class] = digest
    end
end

local digest_mt = {}

local function digest_gc(ctx)
    ffi.C.crypto_EVP_MD_CTX_free(ctx)
end

local function digest_new(digest)
    local ctx = ffi.C.crypto_EVP_MD_CTX_new()
    if ctx == nil then
        return error('Can\'t create digest ctx: ' .. openssl_err_str())
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

local function digest_init(self)
    if self.ctx == nil then
        return error('Digest context isn\'t usable')
    end
    if ffi.C.EVP_DigestInit_ex(self.ctx, self.digest, nil) ~= 1 then
        return error('Can\'t init digest: ' .. openssl_err_str())
    end
    self.initialized = true
end

local function digest_update(self, input)
    if not self.initialized then
        return error('Digest not initialized')
    end
    if ffi.C.EVP_DigestUpdate(self.ctx, input, input:len()) ~= 1 then
        return error('Can\'t update digest: ' .. openssl_err_str())
    end
end

local function digest_final(self)
    if not self.initialized then
        return error('Digest not initialized')
    end
    self.initialized = false
    local ai = ffi.new('int[1]')
    if ffi.C.EVP_DigestFinal_ex(self.ctx, self.buf.wpos, ai) ~= 1 then
        return error('Can\'t finalize digest: ' .. openssl_err_str())
    end
    return ffi.string(self.buf.wpos, ai[0])
end

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

local function hmac_new(digest, key)
    if key == nil then
        return error('Key should be specified for HMAC operations')
    end
    local ctx = ffi.C.crypto_HMAC_CTX_new()
    if ctx == nil then
        return error('Can\'t create HMAC ctx: ' .. openssl_err_str())
    end
    ffi.gc(ctx, hmac_gc)
    local self = setmetatable({
        ctx = ctx,
        digest = digest,
        initialized = false,
    }, hmac_mt)
    self:init(key)
    return self
end

local function hmac_init(self, key)
    if self.ctx == nil then
        return error('HMAC context isn\'t usable')
    end
    if ffi.C.HMAC_Init_ex(self.ctx, key, key:len(), self.digest, nil) ~= 1 then
        return error('Can\'t init HMAC: ' .. openssl_err_str())
    end
    self.initialized = true
end

local function hmac_update(self, input)
    if not self.initialized then
        return error('HMAC not initialized')
    end
    if ffi.C.HMAC_Update(self.ctx, input, input:len()) ~= 1 then
        return error('Can\'t update HMAC: ' .. openssl_err_str())
    end
end

local function hmac_final(self)
    if not self.initialized then
        return error('HMAC not initialized')
    end
    self.initialized = false
    local ibuf = cord_ibuf_take()
    local buf = ibuf:alloc(64)
    local ai = ffi.new('int[1]')
    if ffi.C.HMAC_Final(self.ctx, buf, ai) ~= 1 then
        cord_ibuf_put(ibuf)
        return error('Can\'t finalize HMAC: ' .. openssl_err_str())
    end
    buf = ffi.string(buf, ai[0])
    cord_ibuf_put(ibuf)
    return buf
end

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

local function crypto_stream_begin(self, key, iv)
    local ctx = self.ctx
    if not ctx then
        return error('Cipher context isn\'t usable')
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

local function crypto_stream_append(self, input)
    if not self.is_initialized then
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

local function crypto_stream_commit(self)
    if not self.is_initialized then
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

local hmac_api = {}
for class, digest in pairs(hmacs) do
    hmac_api[class] = setmetatable({
        new = function (key) return hmac_new(digest, key) end
    }, {
        __call = function (self, key, str)
            if type(str) ~= 'string' then
                error("Usage: hmac."..class.."(key, string)")
            end
            local ctx = hmac_new(digest, key)
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

local crypto_algos = {
    none = ffi.C.CRYPTO_ALGO_NONE,
    aes128 = ffi.C.CRYPTO_ALGO_AES128,
    aes192 = ffi.C.CRYPTO_ALGO_AES192,
    aes256 = ffi.C.CRYPTO_ALGO_AES256,
    des = ffi.C.CRYPTO_ALGO_DES
}
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

local crypto_api = setmetatable({}, crypto_api_mt)
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

return setmetatable({
    cipher_algo = crypto_algos,
    cipher_mode = crypto_modes,
}, module_mt)
