digest = require('digest')
type(digest)

digest.md4_hex()
digest.md5_hex()
digest.sha_hex()
digest.sha1_hex()
digest.sha224_hex()
digest.sha256_hex()
digest.sha384_hex()
digest.sha512_hex()

string.len(digest.md4_hex())
string.len(digest.md5_hex())
string.len(digest.sha_hex())
string.len(digest.sha1_hex())
string.len(digest.sha224_hex())
string.len(digest.sha256_hex())
string.len(digest.sha384_hex())
string.len(digest.sha512_hex())

string.len(digest.md4())
string.len(digest.md5())
string.len(digest.sha())
string.len(digest.sha1())
string.len(digest.sha224())
string.len(digest.sha256())
string.len(digest.sha384())
string.len(digest.sha512())

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

digest = nil
