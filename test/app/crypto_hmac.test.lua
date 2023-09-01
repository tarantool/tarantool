test_run = require('test_run').new()
test_run:cmd("push filter ".."'\\.lua.*:[0-9]+: ' to '.lua:<line>\"]: '")

crypto = require('crypto')
type(crypto)


--
-- Invalid arguments
--
crypto.hmac.md4()
crypto.hmac.md5()
crypto.hmac.sha1()
crypto.hmac.sha224()
crypto.hmac.sha256()
crypto.hmac.sha384()
crypto.hmac.sha512()

crypto.hmac.nodigest


string.hex(crypto.hmac.sha1('012345678', 'fred'))

key = '012345678'
message = 'fred'

crypto.hmac.sha1(key, nil)
crypto.hmac.sha1(nil, message)
crypto.hmac.sha1(nil, nil)


string.hex(crypto.hmac.md4(key, message))
string.hex(crypto.hmac.md5(key, message))
string.hex(crypto.hmac.sha1(key, message))
string.hex(crypto.hmac.sha224(key, message))
string.hex(crypto.hmac.sha256(key, message))
string.hex(crypto.hmac.sha384(key, message))
string.hex(crypto.hmac.sha512(key, message))


--
-- Incremental update
--
hmac_sha1 = crypto.hmac.sha1.new(key)
hmac_sha1:update('abc')
hmac_sha1:update('cde')
hmac_sha1:result() == crypto.hmac.sha1(key, 'abccde')


--
-- Empty string
--
string.hex(crypto.hmac.md4(key, ''))
string.hex(crypto.hmac.md5(key, ''))
string.hex(crypto.hmac.sha1(key, ''))
string.hex(crypto.hmac.sha224(key, ''))
string.hex(crypto.hmac.sha256(key, ''))
string.hex(crypto.hmac.sha384(key, ''))
string.hex(crypto.hmac.sha512(key, ''))




test_run:cmd("clear filter")
