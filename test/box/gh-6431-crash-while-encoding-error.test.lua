json = require('json')
msgpack = require('msgpack')
yaml = require('yaml')

e = box.error.new(box.error.ILLEGAL_PARAMS, 'test')
v = {123, e, 'abc'}
box.tuple.new(v)
t = msgpack.decode(msgpack.encode(v))
t
json.encode(v)
yaml.encode(v)
