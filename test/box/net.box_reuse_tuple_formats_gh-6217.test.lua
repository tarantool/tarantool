net = require('net.box')
errinj = box.error.injection

box.schema.user.grant('guest', 'execute', 'universe')

-- Check that formats created by net.box for schema are reused (gh-6217).
COUNT = 100
errinj.set('ERRINJ_TUPLE_FORMAT_COUNT', COUNT)
connections = {}
for i = 1, COUNT do                                                         \
    local c = net.connect(box.cfg.listen)                                   \
    c:eval('return')                                                        \
    table.insert(connections, c)                                            \
end
errinj.set('ERRINJ_TUPLE_FORMAT_COUNT', -1)

#connections == COUNT
for _, c in pairs(connections) do c:close() end

box.schema.user.revoke('guest', 'execute', 'universe')
