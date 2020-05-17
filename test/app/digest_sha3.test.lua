test_run = require('test_run').new()
test_run:cmd("push filter ".."'\\.lua.*:[0-9]+: ' to '.lua:<line>\"]: '")

digest = require('digest')
type(digest)

--
-- Invalid arguments
--
digest.sha3_224()
digest.sha3_256()
digest.sha3_384()
digest.sha3_512()

digest.sha3_224_hex()
digest.sha3_256_hex()
digest.sha3_384_hex()
digest.sha3_512_hex()

--
-- gh-1561: Bad checksum on non-string types
--
digest.sha3_224(12345LL)
digest.sha3_256(12345LL)
digest.sha3_384(12345LL)
digest.sha3_512(12345LL)

--
-- Empty string
--
digest.sha3_224('')
digest.sha3_256('')
digest.sha3_384('')
digest.sha3_512('')

digest.sha3_224_hex('')
digest.sha3_256_hex('')
digest.sha3_384_hex('')
digest.sha3_512_hex('')

--
-- Non-empty string
--
digest.sha3_224('tarantool')
digest.sha3_256('tarantool')
digest.sha3_384('tarantool')
digest.sha3_512('tarantool')

digest.sha3_224_hex('tarantool')
digest.sha3_256_hex('tarantool')
digest.sha3_384_hex('tarantool')
digest.sha3_512_hex('tarantool')

test_run:cmd("clear filter")
