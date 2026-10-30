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
-- msgpack.cfg options are applied to CALL result (i.e., msgpack.cfg options are
-- propagated to the `call_serializer_no_error_ext` serializer).
encode_invalid_numbers = msgpack.cfg.encode_invalid_numbers
msgpack.cfg{                                                                \
    encode_invalid_numbers = false,                                         \
}
type(c:eval('return 0/0')) -- error
msgpack.cfg{                                                                \
    encode_invalid_numbers = encode_invalid_numbers,                        \
}
c:close()
errinj.set('ERRINJ_NETBOX_FLIP_FEATURE', -1)

box.schema.user.revoke('guest', 'super')
