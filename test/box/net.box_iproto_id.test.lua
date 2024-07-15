test_run = require('test_run').new()
net = require('net.box')
errinj = box.error.injection

function print_features(conn)                                               \
    local f = c.peer_protocol_features                                      \
    f.fetch_snapshot_cursor = nil                                           \
    return f                                                                \
end

-- actual version and feautures
c = net.connect(box.cfg.listen)
c.peer_protocol_version
print_features(c)
c:close()

-- no IPROTO_ID => assume no features
errinj.set('ERRINJ_IPROTO_DISABLE_ID', true)
c = net.connect(box.cfg.listen)
c.error -- none
c.peer_protocol_version
print_features(c)
errinj.set('ERRINJ_IPROTO_DISABLE_ID', false)

-- required version
errinj.set('ERRINJ_IPROTO_SET_VERSION', 9000)
c = net.connect(box.cfg.listen, {required_protocol_version = 8999})
c.error -- none
c:close()
c = net.connect(box.cfg.listen, {required_protocol_version = 9000})
c.error -- none
c:close()
c = net.connect(box.cfg.listen, {required_protocol_version = 9001})
c.error -- error
c.peer_protocol_version
print_features(c)
c:close()
errinj.set('ERRINJ_IPROTO_SET_VERSION', -1)

-- required features
c = net.connect(box.cfg.listen, {required_protocol_features = {}})
c.error -- none
c:close()
c = net.connect(box.cfg.listen,                                             \
                {required_protocol_features = {'streams', 'transactions'}})
c.error -- none
c:close()
errinj.set('ERRINJ_IPROTO_FLIP_FEATURE', 1) -- clear transactions feature
c = net.connect(box.cfg.listen,                                             \
                {required_protocol_features = {'streams', 'transactions'}})
c.error -- error
c.peer_protocol_version
print_features(c)
c:close()
errinj.set('ERRINJ_IPROTO_FLIP_FEATURE', -1)
c = net.connect(box.cfg.listen,                                             \
                {required_protocol_features = {'foo', 'transactions', 'bar'}})
c.error -- error
c.peer_protocol_version
print_features(c)
c:close()

-- required features and version are checked on reconnect
timeout = 0.001
c = net.connect(box.cfg.listen, {                                           \
    reconnect_after = timeout,                                              \
    required_protocol_version = 1,                                          \
    required_protocol_features = {'streams'},                               \
})
c.error -- none
errinj.set('ERRINJ_NETBOX_IO_ERROR', true)
c:ping{timeout = timeout} -- false
err = c.error
err -- injection
errinj.set('ERRINJ_NETBOX_IO_ERROR', false)
c:ping() -- true
errinj.set('ERRINJ_NETBOX_IO_ERROR', true)
c:ping{timeout = timeout} -- false
err = c.error
err -- injection
errinj.set('ERRINJ_IPROTO_SET_VERSION', 0)
errinj.set('ERRINJ_NETBOX_IO_ERROR', false)
test_run:wait_cond(function() return c.error ~= err end)
err = c.error
err -- old version
errinj.set('ERRINJ_IPROTO_FLIP_FEATURE', 0) -- clear streams feature
errinj.set('ERRINJ_IPROTO_SET_VERSION', -1)
test_run:wait_cond(function() return c.error ~= err end)
err = c.error
err -- missing features
errinj.set('ERRINJ_IPROTO_FLIP_FEATURE', -1)
c:ping() -- true
c:close()
