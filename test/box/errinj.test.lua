errinj = box.error.injection
net_box = require('net.box')

space = box.schema.space.create('tweedledum')
index = space:create_index('primary', { type = 'hash' })

errinj.info()
errinj.set("some-injection", true)
errinj.set("some-injection") -- check error
space:select{222444}
errinj.set("ERRINJ_TESTING", true)
space:select{222444}
errinj.set("ERRINJ_TESTING", false)

-- Check how well we handle a failed log write
errinj.set("ERRINJ_WAL_IO", true)
space:insert{1}
space:get{1}
errinj.set("ERRINJ_WAL_IO", false)
space:insert{1}
errinj.set("ERRINJ_WAL_IO", true)
space:update(1, {{'=', 2, 2}})
space:get{1}
space:get{2}
errinj.set("ERRINJ_WAL_IO", false)
space:truncate()

-- Check a failed log rotation
errinj.set("ERRINJ_WAL_ROTATE", true)
space:insert{1}
space:get{1}
errinj.set("ERRINJ_WAL_ROTATE", false)
space:insert{1}
errinj.set("ERRINJ_WAL_ROTATE", true)
space:update(1, {{'=', 2, 2}})
space:get{1}
space:get{2}
errinj.set("ERRINJ_WAL_ROTATE", false)
space:update(1, {{'=', 2, 2}})
space:get{1}
space:get{2}
errinj.set("ERRINJ_WAL_ROTATE", true)
space:truncate()
errinj.set("ERRINJ_WAL_ROTATE", false)
space:truncate()

space:drop()

-- Check how well we handle a failed log write in DDL
s_disabled = box.schema.space.create('disabled')
s_withindex = box.schema.space.create('withindex')
index1 = s_withindex:create_index('primary', { type = 'hash' })
s_withdata = box.schema.space.create('withdata')
index2 = s_withdata:create_index('primary', { type = 'tree' })
s_withdata:insert{1, 2, 3, 4, 5}
s_withdata:insert{4, 5, 6, 7, 8}
index3 = s_withdata:create_index('secondary', { type = 'hash', parts = {2, 'num', 3, 'num' }})
errinj.set("ERRINJ_WAL_IO", true)
test = box.schema.space.create('test')
s_disabled:create_index('primary', { type = 'hash' })
s_disabled.enabled
s_disabled:insert{0}
s_withindex:create_index('secondary', { type = 'tree', parts = { 2, 'num'} })
s_withindex.index.secondary
s_withdata.index.secondary:drop()
s_withdata.index.secondary.unique
s_withdata:drop()
box.space['withdata'].enabled
index4 = s_withdata:create_index('another', { type = 'tree', parts = { 5, 'num' }, unique = false})
s_withdata.index.another
errinj.set("ERRINJ_WAL_IO", false)
test = box.schema.space.create('test')
index5 = s_disabled:create_index('primary', { type = 'hash' })
s_disabled.enabled
s_disabled:insert{0}
index6 = s_withindex:create_index('secondary', { type = 'tree', parts = { 2, 'num'} })
s_withindex.index.secondary.unique
s_withdata.index.secondary:drop()
s_withdata.index.secondary
s_withdata:drop()
box.space['withdata']
index7 = s_withdata:create_index('another', { type = 'tree', parts = { 5, 'num' }, unique = false})
s_withdata.index.another
test:drop()
s_disabled:drop()
s_withindex:drop()

-- Check transaction rollback when out of memory
env = require('test_run')
test_run = env.new()

s = box.schema.space.create('s')
_ = s:create_index('pk')
errinj.set("ERRINJ_TUPLE_ALLOC", true)
s:auto_increment{}
s:select{}
s:auto_increment{}
s:select{}
s:auto_increment{}
s:select{}
test_run:cmd("setopt delimiter ';'")
box.begin()
    s:insert{1}
box.commit();
s:select{};
box.begin()
    s:insert{1}
    s:insert{2}
box.commit();
s:select{};
box.begin()
    pcall(s.insert, s, {1})
    s:insert{2}
box.commit();
s:select{};
errinj.set("ERRINJ_TUPLE_ALLOC", false);
box.begin()
    s:insert{1}
    errinj.set("ERRINJ_TUPLE_ALLOC", true)
    s:insert{2}
box.commit();
s:select{};
errinj.set("ERRINJ_TUPLE_ALLOC", false);
box.begin()
    s:insert{1}
    errinj.set("ERRINJ_TUPLE_ALLOC", true)
    pcall(s.insert, s, {2})
box.commit();
s:select{};

test_run:cmd("setopt delimiter ''");
errinj.set("ERRINJ_TUPLE_ALLOC", false)

s:drop()
s = box.schema.space.create('test')
_ = s:create_index('test', {parts = {1, 'NUM', 3, 'NUM', 5, 'NUM'}})
s:insert{1, 2, 3, 4, 5, 6}
t = s:select{}[1]
errinj.set("ERRINJ_TUPLE_FIELD", true)
tostring(t[1]) .. tostring(t[2]) ..tostring(t[3]) .. tostring(t[4]) .. tostring(t[5]) .. tostring(t[6])
errinj.set("ERRINJ_TUPLE_FIELD", false)
tostring(t[1]) .. tostring(t[2]) ..tostring(t[3]) .. tostring(t[4]) .. tostring(t[5]) .. tostring(t[6])

s:drop()
s = box.schema.space.create('test')
_ = s:create_index('test', {parts = {2, 'NUM', 4, 'NUM', 6, 'NUM'}})
s:insert{1, 2, 3, 4, 5, 6}
t = s:select{}[1]
errinj.set("ERRINJ_TUPLE_FIELD", true)
tostring(t[1]) .. tostring(t[2]) ..tostring(t[3]) .. tostring(t[4]) .. tostring(t[5]) .. tostring(t[6])
errinj.set("ERRINJ_TUPLE_FIELD", false)
tostring(t[1]) .. tostring(t[2]) ..tostring(t[3]) .. tostring(t[4]) .. tostring(t[5]) .. tostring(t[6])

-- Cleanup
s:drop()
errinj = nil
