-- crypto.lua (internal file)

local ffi = require 'ffi'
local buffer = require('buffer')

ffi.cdef[[
    /* from openssl/err.h */
    unsigned long ERR_get_error(void);
    char *ERR_error_string(unsigned long e, char *buf);
    void ERR_load_ERR_strings(void);
    void ERR_load_crypto_strings(void);

    /* from openssl/evp.h */
    void OpenSSL_add_all_digests();
    void OpenSSL_add_all_ciphers();
    typedef void ENGINE;

    typedef struct {} EVP_MD_CTX;
    typedef struct {} EVP_MD;
    EVP_MD_CTX *EVP_MD_CTX_create(void);
    void EVP_MD_CTX_destroy(EVP_MD_CTX *ctx);
    int EVP_DigestInit_ex(EVP_MD_CTX *ctx, const EVP_MD *type, ENGINE *impl);
    int EVP_DigestUpdate(EVP_MD_CTX *ctx, const void *d, size_t cnt);
    int EVP_DigestFinal_ex(EVP_MD_CTX *ctx, unsigned char *md, unsigned int *s);
    const EVP_MD *EVP_get_digestbyname(const char *name);

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

    int EVP_CIPHER_block_size(const EVP_CIPHER *cipher);
    const EVP_CIPHER *EVP_get_cipherbyname(const char *name);
]]

ffi.C.OpenSSL_add_all_digests()
ffi.C.OpenSSL_add_all_ciphers()
ffi.C.ERR_load_crypto_strings()

local function openssl_err_str()
  return ffi.string(ffi.C.ERR_error_string(ffi.C.ERR_get_error(), nil))
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
    ffi.C.EVP_MD_CTX_destroy(ctx)
end

local function digest_new(digest)
    local ctx = ffi.C.EVP_MD_CTX_create()
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
    ffi.C.EVP_MD_CTX_destroy(self.ctx)
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
    if key == nil or key:len() ~= ffi.C.tnt_EVP_CIPHER_key_length(cipher) then
        return error('Key length should be equal to cipher key length ('
            .. tostring(ffi.C.tnt_EVP_CIPHER_key_length(cipher)) .. ' bytes)')
    end
    if iv == nil or iv:len() ~= ffi.C.tnt_EVP_CIPHER_iv_length(cipher) then
        return error('Initial vector length should be equal to cipher iv length ('
            .. tostring(ffi.C.tnt_EVP_CIPHER_iv_length(cipher)) .. ' bytes)')
    end
    local ctx = ffi.C.EVP_CIPHER_CTX_new()
    if ctx == nil then
        return error('Can\'t create cipher ctx: ' .. openssl_err_str())
    end
    ffi.gc(ctx, cipher_gc)
    local self = setmetatable({
        ctx = ctx,
        cipher = cipher,
        block_size = ffi.C.EVP_CIPHER_block_size(cipher),
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
    if ffi.C.EVP_CipherInit_ex(self.ctx, self.cipher, nil,
        key, iv, self.direction) ~= 1 then
        return error('Can\'t init cipher:' .. openssl_err_str())
    end
    self.initialized = true
end

local function cipher_update(self, input)
    if not self.initialized then
        return error('Cipher not initialized')
    end
    if input == nil then
      return ''
    end
    input = tostring(input)
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
    self.initialized = false
    local wpos = self.buf:reserve(self.block_size - 1)
    if ffi.C.EVP_CipherFinal_ex(self.ctx, wpos, self.outl) ~= 1 then
        return error('Can\'t finalize cipher:' .. openssl_err_str())
    end
    self.initialized = false
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
            if str == nil then
                str = ''
            else
                str = tostring(str)
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
    cipher = cipher_api,
}
