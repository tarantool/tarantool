buffer = require 'buffer'
msgpack = require 'msgpack'
ffi = require 'ffi'

-- Arguments check.
buf = buffer.ibuf()
msgpack.encode()
msgpack.encode('test', 'str')
msgpack.encode('test', buf.buf)
msgpack.decode()
msgpack.decode(123)
msgpack.decode(buf)
msgpack.decode(buf.buf, 'size')
msgpack.decode('test', 0)
msgpack.decode('test', 5)
msgpack.decode('test', 'offset')
msgpack.decode_unchecked()
msgpack.decode_unchecked(123)
msgpack.decode_unchecked(buf)
msgpack.decode_unchecked('test', 0)
msgpack.decode_unchecked('test', 5)
msgpack.decode_unchecked('test', 'offset')

-- Encode/decode a string.
s = msgpack.encode({1, 2, 3}) .. msgpack.encode({4, 5, 6})
obj, offset = msgpack.decode(s)
obj
obj, offset = msgpack.decode(s, offset)
obj
offset == #s + 1
obj, offset = msgpack.decode_unchecked(s)
obj
obj, offset = msgpack.decode_unchecked(s, offset)
obj
offset == #s + 1

-- Encode/decode a buffer.
buf = buffer.ibuf()
len = msgpack.encode({1, 2, 3}, buf)
len = msgpack.encode({4, 5, 6}, buf) + len
buf:size() == len
orig_rpos = buf.rpos
obj, rpos = msgpack.decode(buf.rpos, buf:size())
obj
buf.rpos = rpos
obj, rpos = msgpack.decode(buf.rpos, buf:size())
obj
buf.rpos = rpos
buf:size() == 0
buf.rpos = orig_rpos
obj, rpos = msgpack.decode_unchecked(buf.rpos, buf:size())
obj
buf.rpos = rpos
obj, rpos = msgpack.decode_unchecked(buf.rpos, buf:size())
obj
buf.rpos = rpos
buf:size() == 0

-- Invalid msgpack.
s = msgpack.encode({1, 2, 3})
s = s:sub(1, -2)
msgpack.decode(s)
buf = buffer.ibuf()
msgpack.encode({1, 2, 3}, buf)
msgpack.decode(buf.rpos, buf:size() - 1)

-- Provide a buffer. Try both 'struct ibuf' and 'struct ibuf *'.
buf_storage = buffer.ibuf()
buf = ffi.cast('struct ibuf *', buf_storage)
size = msgpack.encode({a = 1, b = 2}, buf)
(msgpack.decode(buf.rpos, size))
buf_storage = nil
buf = buffer.ibuf()
size = msgpack.encode({c = 3, d = 4}, buf)
(msgpack.decode(buf.rpos, size))

-- Decode should accept both 'char *' and 'const char *'.
buf:reset()
size = msgpack.encode(100, buf)
(msgpack.decode(ffi.cast('char *', buf.rpos), size))
(msgpack.decode(ffi.cast('const char *', buf.rpos), size))

--
-- gh-4224: msgpack.decode(cdata, size) should check, that size
-- is not negative.
--
msgpack.decode(ffi.cast('char *', '\x04\x05\x06'), -1)

--
-- gh-4333: msgpack encode/decode decimals.
--
decimal = require('decimal')
a = decimal.new('1e37')
b = decimal.new('1e-38')
c = decimal.new('1')
d = decimal.new('0.1234567')
e = decimal.new('123.4567')
msgpack.decode(msgpack.encode(a)) == a
msgpack.decode(msgpack.encode(b)) == b
msgpack.decode(msgpack.encode(c)) == c
msgpack.decode(msgpack.encode(d)) == d
msgpack.decode(msgpack.encode(e)) == e

--
-- gh-4268: msgpack encode/decode UUID
--
uuid = require('uuid')
fail = nil
for i = 1,10 do\
    local a = uuid.new()\
    if msgpack.decode(msgpack.encode(a)) ~= a then\
        fail = a\
    end\
end
fail
