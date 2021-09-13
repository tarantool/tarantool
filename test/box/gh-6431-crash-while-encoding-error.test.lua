json = require('json')
msgpack = require('msgpack')
yaml = require('yaml')

error_marshaling_enabled = box.session.settings.error_marshaling_enabled
box.session.settings.error_marshaling_enabled = true

e = box.error.new(box.error.ILLEGAL_PARAMS, 'test')
v = {123, e, 'abc'}
box.tuple.new(v)
t = msgpack.decode(msgpack.encode(v))
t
json.encode(v)
yaml.encode(v)

box.session.settings.error_marshaling_enabled = error_marshaling_enabled
