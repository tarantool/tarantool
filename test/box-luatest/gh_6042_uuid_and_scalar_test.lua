local server = require('luatest.server')
local t = require('luatest')

local g = t.group()

g.before_all(function()
    g.server = server:new({alias = 'uuid_scalar'})
    g.server:start()
end)

g.after_all(function()
    g.server:stop()
end)

g.test_insert_uuid_into_scalar = function()
    g.server:exec(function()
        local uuid = require('uuid')
        local u1 = uuid.fromstr('11111111-1111-1111-1111-111111111111')
        local u2 = uuid.fromstr('11111111-1111-1111-1111-111111111112')
        local s = box.schema.space.create('s', {format={{'s', 'scalar'}}})
        local _ = s:create_index('i')
        s:insert({u1})
        s:insert({u2})
        s:insert({1})
        s:insert({'1'})
        s:insert({true})
        box.execute([[INSERT INTO "s" VALUES (x'303030')]])
        t.assert_equals(s:select(), {
            box.tuple.new({true}),
            box.tuple.new({1}),
            box.tuple.new({'1'}),
            box.tuple.new({'\x30\x30\x30'}),
            box.tuple.new({u1}),
            box.tuple.new({u2}),
        })
        t.assert_equals(s:select({}, {iterator='LE'}), {
            box.tuple.new({u2}),
            box.tuple.new({u1}),
            box.tuple.new({'\x30\x30\x30'}),
            box.tuple.new({'1'}),
            box.tuple.new({1}),
            box.tuple.new({true}),
        })
        s:drop()
    end)
end

g.test_uuid_scalar = function()
    g.server:exec(function()
        local s = box.schema.space.create('a', {format = {{'u', 'uuid'}}})
        local _ = s:create_index('i', {parts = {{'u', 'scalar'}}})
        t.assert_equals(s:format()[1].type, 'uuid')
        t.assert_equals(s.index[0].parts[1].type, 'scalar')
        s:drop()

        s = box.schema.space.create('a', {format = {{'u', 'scalar'}}})
        _ = s:create_index('i', {parts = {{'u', 'uuid'}}})
        t.assert_equals(s:format()[1].type, 'scalar')
        t.assert_equals(s.index[0].parts[1].type, 'uuid')
        s:drop()
    end)
end
