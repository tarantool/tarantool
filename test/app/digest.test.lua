test_run = require('test_run').new()
test_run:cmd("push filter ".."'\\.lua.*:[0-9]+: ' to '.lua:<line>\"]: '")

fiber = require('fiber')
digest = require('digest')
type(digest)


--
-- Invalid arguments
--
digest.md4()
digest.md5()
digest.sha1()
digest.sha224()
digest.sha256()
digest.sha384()
digest.sha512()

digest.md4_hex()
digest.md5_hex()
digest.sha1_hex()
digest.sha224_hex()
digest.sha256_hex()
digest.sha384_hex()
digest.sha512_hex()

--
-- gh-1561: Bad checksum on non-string types
--
digest.md4(12345LL)
digest.md5(12345LL)
digest.sha1(12345LL)
digest.sha224(12345LL)
digest.sha256(12345LL)
digest.sha384(12345LL)
digest.sha512(12345LL)

--
-- Empty string
--
string.hex(digest.md4(''))
string.hex(digest.md5(''))
string.hex(digest.sha1(''))
string.hex(digest.sha224(''))
string.hex(digest.sha256(''))
string.hex(digest.sha384(''))
string.hex(digest.sha512(''))

digest.md4_hex('')
digest.md5_hex('')
digest.sha1_hex('')
digest.sha224_hex('')
digest.sha256_hex('')
digest.sha384_hex('')
digest.sha512_hex('')

--
-- Non-empty string
--
string.hex(digest.md4('tarantool'))
string.hex(digest.md5('tarantool'))
string.hex(digest.sha1('tarantool'))
string.hex(digest.sha224('tarantool'))
string.hex(digest.sha256('tarantool'))
string.hex(digest.sha384('tarantool'))
string.hex(digest.sha512('tarantool'))

digest.md4_hex('tarantool')
digest.md5_hex('tarantool')
digest.sha1_hex('tarantool')
digest.sha224_hex('tarantool')
digest.sha256_hex('tarantool')
digest.sha384_hex('tarantool')
digest.sha512_hex('tarantool')

digest.md5_hex(123)
digest.md5_hex('123')
digest.md5_hex(true)
digest.md5_hex('true')
digest.md5_hex(nil)
digest.md5_hex()

digest.crc32()
digest.crc32_update(4294967295, '')

digest.crc32('abc')
digest.crc32_update(4294967295, 'abc')

digest.crc32('abccde')
digest.crc32_update(digest.crc32('abc'), 'cde')

crc = digest.crc32.new()
crc:update('abc')
crc2 = crc:copy()
crc:update('cde')
crc:result() == digest.crc32('abccde')
crc2:update('def')
crc2:result() == digest.crc32('abcdef')
crc, crc2 = nil, nil

digest.base64_encode('12345')
digest.base64_decode('MTIzNDU=')
digest.base64_encode('asdfl asdf adfa zxc vzxcvz llll')
digest.base64_decode('YXNkZmwgYXNkZiBhZGZhIHp4YyB2enhjdnogbGxsbA==')
digest.base64_encode('11 00 11 00 abcdef ABCDEF 00 11 00 11')
digest.base64_decode('MTEgMDAgMTEgMDAgYWJjZGVmIEFCQ0RFRiAwMCAxMSAwMCAxMQ==')
s = string.rep('a', 54 * 2) -- two lines in base64
b = digest.base64_encode(s)
b
digest.base64_decode(b) == s
digest.base64_decode(nil)
digest.base64_encode(nil)
digest.base64_encode(123)
digest.base64_decode(123)

digest.guava('hello', 0)
digest.guava(1, 'nope_')
digest.guava(10863919174838991, 11)
digest.guava(2016238256797177309, 11)
digest.guava(1673758223894951030, 11)

digest.urandom()
#digest.urandom(0)
#digest.urandom(1)
#digest.urandom(16)

digest.murmur('1234')
mur = digest.murmur.new{seed=13}
nulldigest = mur:result()
mur:update('1234')
mur:result()
mur_new = mur:copy()
mur_new:update('1234')
mur_new:result() ~= mur:result()
mur:clear()
nulldigest == mur:result()
mur = digest.murmur.new{seed=14}
mur:update('1234')
mur:result()
mur, mur_new, nulldigest = nil, nil, nil

string.hex(digest.aes256cbc.encrypt('test123', 'passpasspasspasspasspasspasspass', 'iv12tras8712cvbh'))
digest.aes256cbc.decrypt(digest.aes256cbc.encrypt('test123', 'passpasspasspasspasspasspasspass', 'iv12tras8712cvbh'), 'passpasspasspasspasspasspasspass', 'iv12tras8712cvbh')
digest.aes256cbc.decrypt(digest.aes256cbc.encrypt('test123', 'passpasspasspasspasspasspasspass', 'iv12tras8712cvbh'), 'nosspasspasspasspasspasspasspass', 'iv12tras8712cvbh')

--
-- Test base64 options. (gh-2479, gh-2478, gh-2777).
--
b = digest.base64_encode('123', { urlsafe = true })
b
digest.base64_decode(b)
b = digest.base64_encode('1234567', { urlsafe = true })
b
digest.base64_decode(b)
b = digest.base64_encode('12345678', { urlsafe = true })
b
digest.base64_decode(b)
b = digest.base64_encode('1234567', { nopad = true })
b
digest.base64_decode(b)
b = digest.base64_encode(string.rep('a', 100), { nowrap = true })
b
digest.base64_decode(b)

--
-- gh-3358: any option makes base64 work like urlsafe.
--
s = digest.base64_encode('?>>>', {nowrap = true})
-- Check for '+' - it is not urlsafe.
s:find('+') ~= nil
s = digest.base64_encode('?>>>', {nopad = true})
s:find('+') ~= nil

string.hex(digest.pbkdf2("password", "salt", 4096, 32))
digest.pbkdf2_hex("password", "salt", 4096, 32)
digest.pbkdf2_hex("password", "salt")
s, err = pcall(digest.pbkdf2, 12, "salt")
s
err:match("Usage")
s, err = pcall(digest.pbkdf2_hex, 12, "salt")
s
err:match("Usage")
s, err = pcall(digest.pbkdf2_hex, "password", "salt", "lol", "lol")
s
err:match("number")

-- gh-3396: fiber-safe pbkdf2
res = {}
sentry = fiber.channel()
_ = test_run:cmd("setopt delimiter ';'")
function test_pbkdf2()
    local digest = require('digest')
    for i = 1, 10 do
        table.insert(res, digest.pbkdf2('', 'salt', 100, 32):hex())
    end
    sentry:put(fiber.id())
end;
_ = test_run:cmd("setopt delimiter ''");
_ = fiber.create(test_pbkdf2)
_ = fiber.create(test_pbkdf2)
_ = sentry:get()
_ = sentry:get()
res

--
-- gh-2003 xxHash.
--
xxhash32 = digest.xxhash32.new()
xxhash32:result()
xxhash64 = digest.xxhash64.new()
xxhash64:result()

-- New takes seed optionally.
digest.xxhash32.new(1):result()
digest.xxhash64.new(1):result()

-- String is expected as input value.
digest.xxhash32(1)
digest.xxhash64(1)
digest.xxhash32.new():update(1)
digest.xxhash64.new():update(1)

-- Seed is an optional second argument (default = 0).
digest.xxhash32('12345')
digest.xxhash32('12345', 0)
digest.xxhash32('12345', 1)
xxhash32:result()
xxhash32:clear(1)
xxhash32:result()
xxhash32:update('123')
xxhash32:result()
xxhash32:update('45')
xxhash32:result()
xxhash32:clear()
xxhash32:result()
xxhash32_copy = xxhash32:copy()
xxhash32_copy:result()
xxhash32_copy ~= xxhash32
xxhash32_copy:clear(1ULL)
xxhash32_copy:result()
xxhash32 = nil
xxhash32_copy = nil

-- Seed is an optional second argument (default = 0).
digest.xxhash64('12345')
digest.xxhash64('12345', 0)
digest.xxhash64('12345', 1)
xxhash64:result()
xxhash64:clear(1)
xxhash64:result()
xxhash64:update('123')
xxhash64:result()
xxhash64:update('45')
xxhash64:result()
xxhash64:clear()
xxhash64:result()
xxhash64_copy = xxhash64:copy()
xxhash64_copy:result()
xxhash64_copy ~= xxhash64
xxhash64_copy:clear(1ULL)
xxhash64_copy:result()
xxhash64 = nil
xxhash64_copy = nil

test_run:cmd("clear filter")
digest = nil
