test_run = require('test_run').new()
test_run:cmd("push filter ".."'\\.lua.*:[0-9]+: ' to '.lua:<line>\"]: '")

crypto = require('crypto')
type(crypto)

ciph = crypto.cipher.aes128.cbc
pass = '1234567887654321'
iv = 'abcdefghijklmnop'
enc = ciph.encrypt('test', pass, iv)
enc
ciph.decrypt(enc, pass, iv)


-- Failing scenarios.
crypto.cipher.aes128.cbc.encrypt('a')
crypto.cipher.aes128.cbc.encrypt('a', '123456', '435')
crypto.cipher.aes128.cbc.encrypt('a', '1234567887654321')
crypto.cipher.aes128.cbc.encrypt('a', '1234567887654321', '12')

crypto.cipher.aes256.cbc.decrypt('a')
crypto.cipher.aes256.cbc.decrypt('a', '123456', '435')
crypto.cipher.aes256.cbc.decrypt('a', '12345678876543211234567887654321')
crypto.cipher.aes256.cbc.decrypt('12', '12345678876543211234567887654321', '12')

crypto.cipher.aes192.cbc.decrypt.new('123456788765432112345678', '12345')

-- Set key after codec creation.
c = crypto.cipher.aes128.cbc.encrypt.new()
key = '1234567812345678'
iv = key
c:init(key)
c:update('plain')
c:result()
c:init(nil, iv)
cipher = c:update('plain ')
cipher = cipher..c:update('next plain')
cipher = cipher..c:result()
crypto.cipher.aes128.cbc.decrypt(cipher, key, iv)
-- Reuse.
key2 = '8765432187654321'
iv2 = key2
c:init(key2, iv2)
cipher = c:update('new plain ')
cipher = cipher..c:update('next new plain')
cipher = cipher..c:result()
crypto.cipher.aes128.cbc.decrypt(cipher, key2, iv2)

crypto.cipher.aes100.efb
crypto.cipher.aes256.nomode

crypto.digest.nodigest

-- Check that GC really drops unused codecs and streams, and
-- nothing crashes.
weak = setmetatable({obj = c}, {__mode = 'v'})
c = nil
collectgarbage('collect')
weak.obj

bad_pass = '8765432112345678'
bad_iv = '123456abcdefghij'
ciph.decrypt(enc, bad_pass, iv)
ciph.decrypt(enc, pass, bad_iv)

test_run:cmd("clear filter")
