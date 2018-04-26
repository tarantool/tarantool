test_run = require('test_run').new()
test_run:cmd("push filter ".."'\\.lua.*:[0-9]+: ' to '.lua:<line>\"]: '")

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
digest.md4('')
digest.md5('')
digest.sha1('')
digest.sha224('')
digest.sha256('')
digest.sha384('')
digest.sha512('')

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
digest.md4('tarantool')
digest.md5('tarantool')
digest.sha1('tarantool')
digest.sha224('tarantool')
digest.sha256('tarantool')
digest.sha384('tarantool')
digest.sha512('tarantool')

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

digest.aes256cbc.encrypt('test123', 'passpasspasspasspasspasspasspass', 'iv12tras8712cvbh')
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

digest.pbkdf2("password", "salt", 4096, 32)
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
digest = nil
test_run:cmd("clear filter")
