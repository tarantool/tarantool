test_run = require('test_run').new()
test_run:cmd("push filter ".."'\\.lua.*:[0-9]+: ' to '.lua:<line>\"]: '")

crypto = require('crypto')
type(crypto)

--
-- Invalid arguments
--
crypto.hmac.sha3_224()
crypto.hmac.sha3_256()
crypto.hmac.sha3_384()
crypto.hmac.sha3_512()

key = '012345678'
message = 'fred'

crypto.hmac.sha3_224(key, message)
crypto.hmac.sha3_256(key, message)
crypto.hmac.sha3_384(key, message)
crypto.hmac.sha3_512(key, message)

test_run:cmd("clear filter")
