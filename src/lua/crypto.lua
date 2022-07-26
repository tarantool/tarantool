-- crypto.lua (internal file)

local ffi = require('ffi')
local buffer = require('buffer')

ffi.cdef[[
    int tnt_openssl_init(void);
    /* from openssl/err.h */
    unsigned long ERR_get_error(void);
    char *ERR_reason_error_string(unsigned long e);

    /* from openssl/evp.h */
    typedef void ENGINE;

    typedef struct {} EVP_MD_CTX;
    typedef struct {} EVP_MD;
    EVP_MD_CTX *tnt_EVP_MD_CTX_new(void);
    void tnt_EVP_MD_CTX_free(EVP_MD_CTX *ctx);
    int EVP_DigestInit_ex(EVP_MD_CTX *ctx, const EVP_MD *type, ENGINE *impl);
    int EVP_DigestUpdate(EVP_MD_CTX *ctx, const void *d, size_t cnt);
    int EVP_DigestFinal_ex(EVP_MD_CTX *ctx, unsigned char *md, unsigned int *s);
    const EVP_MD *EVP_get_digestbyname(const char *name);

    typedef struct {} tnt_HMAC_CTX;
    tnt_HMAC_CTX *tnt_HMAC_CTX_new(void);
    void tnt_HMAC_CTX_free(tnt_HMAC_CTX *ctx);
    int tnt_HMAC_Init_ex(tnt_HMAC_CTX *ctx, const void *key, int len,
                         const char *digest, const EVP_MD *md, ENGINE *impl);
    int tnt_HMAC_Update(tnt_HMAC_CTX *ctx, const unsigned char *data,
                        size_t len);
    int tnt_HMAC_Final(tnt_HMAC_CTX *ctx, unsigned char *md, unsigned int *len,
                       unsigned int size);

    typedef struct {} EVP_CIPHER_CTX;
    typedef struct {} EVP_CIPHER;
    EVP_CIPHER_CTX *EVP_CIPHER_CTX_new();
    void EVP_CIPHER_CTX_free(EVP_CIPHER_CTX *ctx);

    int EVP_CipherInit_ex(EVP_CIPHER_CTX *ctx, const EVP_CIPHER *cipher,
                          ENGINE *impl, const unsigned char *key,
                          const unsigned char *iv, int enc);
    int EVP_CipherUpdate(EVP_CIPHER_CTX *ctx, unsigned char *out, int *outl,
                     const unsigned char *in, int inl);
    int EVP_CipherFinal_ex(EVP_CIPHER_CTX *ctx, unsigned char *out, int *outl);
    int EVP_CIPHER_CTX_cleanup(EVP_CIPHER_CTX *ctx);

    int tnt_EVP_CIPHER_iv_length(const EVP_CIPHER *cipher);
    int tnt_EVP_CIPHER_key_length(const EVP_CIPHER *cipher);

    int tnt_EVP_CIPHER_block_size(const EVP_CIPHER *cipher);
    const EVP_CIPHER *EVP_get_cipherbyname(const char *name);
]]

ffi.C.tnt_openssl_init();

local function openssl_err_str()
  return ffi.string(ffi.C.ERR_reason_error_string(ffi.C.ERR_get_error()))
end

local digests = {}
for class, name in pairs({
    md2 = 'MD2', md4 = 'MD4', md5 = 'MD5',
    sha = 'SHA', sha1 = 'SHA1', sha224 = 'SHA224',
    sha256 = 'SHA256', sha384 = 'SHA384', sha512 = 'SHA512',
    dss = 'DSS', dss1 = 'DSS1', mdc2 = 'MDC2', ripemd160 = 'RIPEMD160'}) do
    local digest = ffi.C.EVP_get_digestbyname(class)
    if digest ~= nil then
        digests[class] = digest
    end
end

local digest_mt = {}

local function digest_gc(ctx)
    ffi.C.tnt_EVP_MD_CTX_free(ctx)
end

local function digest_new(digest)
    local ctx = ffi.C.tnt_EVP_MD_CTX_new()
    if ctx == nil then
        return error('Can\'t create digest ctx: ' .. openssl_err_str())
    end
    ffi.gc(ctx, digest_gc)
    local self = setmetatable({
        ctx = ctx,
        digest = digest,
        buf = buffer.ibuf(64),
        initialized = false,
        outl = ffi.new('int[1]')
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
    if ffi.C.EVP_DigestFinal_ex(self.ctx, self.buf.wpos, self.outl) ~= 1 then
        return error('Can\'t finalize digest: ' .. openssl_err_str())
    end
    return ffi.string(self.buf.wpos, self.outl[0])
end

local function digest_free(self)
    ffi.C.tnt_EVP_MD_CTX_free(self.ctx)
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
    ffi.C.tnt_HMAC_CTX_free(ctx)
end

local function hmac_new(class, digest, key)
    if key == nil then
        return error('Key should be specified for HMAC operations')
    end
    local ctx = ffi.C.tnt_HMAC_CTX_new()
    if ctx == nil then
        return error('Can\'t create HMAC ctx: ' .. openssl_err_str())
    end
    ffi.gc(ctx, hmac_gc)
    local self = setmetatable({
        ctx = ctx,
        class = class,
        digest = digest,
        buf = buffer.ibuf(64),
        initialized = false,
        outl = ffi.new('int[1]')
    }, hmac_mt)
    self:init(key)
    return self
end

local function hmac_init(self, key)
    if self.ctx == nil then
        return error('HMAC context isn\'t usable')
    end
    if ffi.C.tnt_HMAC_Init_ex(self.ctx, key, key:len(), self.class,
                              self.digest, nil) ~= 1 then
        return error('Can\'t init HMAC: ' .. openssl_err_str())
    end
    self.initialized = true
end

local function hmac_update(self, input)
    if not self.initialized then
        return error('HMAC not initialized')
    end
    if ffi.C.tnt_HMAC_Update(self.ctx, input, input:len()) ~= 1 then
        return error('Can\'t update HMAC: ' .. openssl_err_str())
    end
end

local function hmac_final(self)
    if not self.initialized then
        return error('HMAC not initialized')
    end
    self.initialized = false
    if ffi.C.tnt_HMAC_Final(self.ctx, self.buf.wpos, self.outl,
                            self.buf:unused()) ~= 1 then
        return error('Can\'t finalize HMAC: ' .. openssl_err_str())
    end
    return ffi.string(self.buf.wpos, self.outl[0])
end

local function hmac_free(self)
    ffi.C.tnt_HMAC_CTX_free(self.ctx)
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

local ciphers = {}
for algo, algo_name in pairs({des = 'DES', aes128 = 'AES-128',
    aes192 = 'AES-192', aes256 = 'AES-256'}) do
    local algo_api = {}
    for mode, mode_name in pairs({cfb = 'CFB', ofb = 'OFB',
        cbc = 'CBC', ecb = 'ECB'}) do
            local cipher =
                ffi.C.EVP_get_cipherbyname(algo_name .. '-' .. mode_name)
            if cipher ~= nil then
                algo_api[mode] = cipher
            end
    end
    if algo_api ~= {} then
        ciphers[algo] = algo_api
    end
end

local cipher_mt = {}

local function cipher_gc(ctx)
    ffi.C.EVP_CIPHER_CTX_free(ctx)
end

local function cipher_new(cipher, key, iv, direction)
    local ctx = ffi.C.EVP_CIPHER_CTX_new()
    if ctx == nil then
        return error('Can\'t create cipher ctx: ' .. openssl_err_str())
    end
    ffi.gc(ctx, cipher_gc)
    local self = setmetatable({
        ctx = ctx,
        cipher = cipher,
        block_size = ffi.C.tnt_EVP_CIPHER_block_size(cipher),
        direction = direction,
        buf = buffer.ibuf(),
        initialized = false,
        outl = ffi.new('int[1]')
    }, cipher_mt)
    self:init(key, iv)
    return self
end

local function cipher_init(self, key, iv)
    if self.ctx == nil then
        return error('Cipher context isn\'t usable')
    end
    local cipher = self.cipher
    key = key or self.key
    iv = iv or self.iv
    local needed = ffi.C.tnt_EVP_CIPHER_key_length(cipher)
    if key ~= nil and key:len() ~= needed then
        return error('Key length should be equal to cipher key length ('..
                     tostring(needed)..' bytes)')
    end
    needed = ffi.C.tnt_EVP_CIPHER_iv_length(cipher)
    if iv ~= nil and iv:len() ~= needed then
        return error('Initial vector length should be equal to cipher iv '..
                     'length ('..tostring(needed)..' bytes)')
    end
    self.key = key
    self.iv = iv
    if key and iv then
        if ffi.C.EVP_CipherInit_ex(self.ctx, cipher, nil, key, iv,
                                   self.direction) ~= 1 then
            return error('Can\'t init cipher:'..openssl_err_str())
        end
        self.initialized = true
    end
end

local function cipher_update(self, input)
    if not self.initialized then
        return error('Cipher not initialized')
    end
    if type(input) ~= 'string' then
        error("Usage: cipher:update(string)")
    end
    local wpos = self.buf:reserve(input:len() + self.block_size - 1)
    if ffi.C.EVP_CipherUpdate(self.ctx, wpos, self.outl, input, input:len()) ~= 1 then
        return error('Can\'t update cipher:' .. openssl_err_str())
    end
    return ffi.string(wpos, self.outl[0])
end

local function cipher_final(self)
    if not self.initialized then
        return error('Cipher not initialized')
    end
    local wpos = self.buf:reserve(self.block_size)
    if ffi.C.EVP_CipherFinal_ex(self.ctx, wpos, self.outl) ~= 1 then
        return error('Can\'t finalize cipher:' .. openssl_err_str())
    end
    return ffi.string(wpos, self.outl[0])
end

local function cipher_free(self)
    ffi.C.EVP_CIPHER_CTX_free(self.ctx)
    ffi.gc(self.ctx, nil)
    self.ctx = nil
    self.initialized = false
    self.buf:reset()
end

cipher_mt = {
    __index = {
          init = cipher_init,
          update = cipher_update,
          result = cipher_final,
          free = cipher_free
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

local function cipher_mode_error(self, mode)
  error('Cipher mode ' .. mode .. ' is not supported')
end

local cipher_api = {}
for class, subclass in pairs(ciphers) do
    local class_api = {}
    for subclass, cipher in pairs(subclass) do
        class_api[subclass] = {}
        for direction, param in pairs({encrypt = 1, decrypt = 0}) do
              class_api[subclass][direction] = setmetatable({
                new = function (key, iv)
                    return cipher_new(cipher, key, iv, param)
                end
            }, {
                __call = function (self, str, key, iv)
                    local ctx = cipher_new(cipher, key, iv, param)
                    local res = ctx:update(str)
                    res = res .. ctx:result()
                    ctx:free()
                    return res
                end
            })
        end
    end
    class_api = setmetatable(class_api, {__index = cipher_mode_error})
    if class_api ~= {} then
        cipher_api[class] = class_api
    end
end

cipher_api = setmetatable(cipher_api,
    {__index = function(self, cipher)
        return error('Cipher method "' .. cipher .. '" is not supported')
    end })

return {
    digest = digest_api,
    hmac   = hmac_api,
    cipher = cipher_api,
}
