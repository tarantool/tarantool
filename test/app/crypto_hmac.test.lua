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


crypto.hmac.sha1('012345678', 'fred')

key = '012345678'
message = 'fred'

crypto.hmac.sha1(key, message)


--
-- Empty string
--
crypto.hmac.md4(key, '')
crypto.hmac.md5(key, '')
crypto.hmac.sha1(key, '')
crypto.hmac.sha224(key, '')
crypto.hmac.sha256(key, '')
crypto.hmac.sha384(key, '')
crypto.hmac.sha512(key, '')


test_run:cmd("clear filter")
