net = require('net.box')
msgpack = require('msgpack')
errinj = box.error.injection

box.schema.user.grant('guest', 'super')

c = net.connect(box.cfg.listen)
type(c:eval('return box.error.new(box.error.UNKNOWN)')) -- cdata
c:close()

errinj.set('ERRINJ_NETBOX_DISABLE_ID', true) -- do not send IPROTO_ID request
c = net.connect(box.cfg.listen)
type(c:eval('return box.error.new(box.error.UNKNOWN)')) -- string
c:close()
errinj.set('ERRINJ_NETBOX_DISABLE_ID', false)

errinj.set('ERRINJ_NETBOX_FLIP_FEATURE', 2) -- clear error_extension feature
c = net.connect(box.cfg.listen)
type(c:eval('return box.error.new(box.error.UNKNOWN)')) -- string
-- msgpack.cfg options are applied to CALL result
encode_load_metatables = msgpack.cfg.encode_load_metatables
encode_use_tostring = msgpack.cfg.encode_use_tostring
msgpack.cfg{                                                                \
    encode_load_metatables = false,                                         \
    encode_use_tostring = false,                                            \
}
type(c:eval('return box.error.new(box.error.UNKNOWN)')) -- error
msgpack.cfg{                                                                \
    encode_load_metatables = encode_load_metatables,                        \
    encode_use_tostring = encode_use_tostring,                              \
}
c:close()
errinj.set('ERRINJ_NETBOX_FLIP_FEATURE', -1)

box.schema.user.revoke('guest', 'super')
