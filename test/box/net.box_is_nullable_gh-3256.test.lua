net = require('net.box')

--
-- gh-3256 net.box is_nullable and collation options output
--
space = box.schema.create_space('test')
box.schema.user.grant('guest', 'read', 'space', 'test')
_ = space:create_index('pk')
_ = space:create_index('sk', {parts = {{2, 'unsigned', is_nullable = true}}})
c = net:connect(box.cfg.listen)
c.space.test.index.sk.parts
space:drop()

space = box.schema.create_space('test')
c:close()
box.schema.user.grant('guest', 'read', 'space', 'test')
c = net:connect(box.cfg.listen)
box.internal.collation.create('test', 'ICU', 'ru-RU')
collation_id = box.internal.collation.id_by_name('test')
_ = space:create_index('sk', { type = 'tree', parts = {{1, 'str', collation = 'test'}}, unique = true })
c:reload_schema()
parts = c.space.test.index.sk.parts
#parts == 1
parts[1].fieldno == 1
parts[1].type == 'string'
parts[1].is_nullable == false
if _TARANTOOL >= '2.2.1' then                    \
    return parts[1].collation == 'test'          \
else                                             \
    return parts[1].collation_id == collation_id \
end
c:close()
box.internal.collation.drop('test')
space:drop()
c.state
c = nil
