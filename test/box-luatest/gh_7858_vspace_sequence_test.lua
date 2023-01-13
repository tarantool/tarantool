local server = require('luatest.server')
local t = require('luatest')

local g = t.group()

g.before_all(function(cg)
    cg.server = server:new({alias = 'default'})
    cg.server:start()
end)

g.after_all(function(cg)
    cg.server:drop()
end)

g.after_test('test_vspace_sequence', function(cg)
    cg.server:exec(function()
        if box.space.test1 ~= nil then
            box.space.test1:drop()
        end
        if box.space.test2 ~= nil then
            box.space.test2:drop()
        end
        if box.sequence.test ~= nil then
            box.sequence.test:drop()
        end
        box.schema.user.drop('alice', {if_exists = true})
    end)
end)

g.test_vspace_sequence = function(cg)
    cg.server:exec(function()
        local t = require('luatest')
        t.assert_equals(box.space._vspace_sequence.id,
                        box.schema.VSPACE_SEQUENCE_ID)
        t.assert_equals(box.space._vspace_sequence:format(),
                        box.space._space_sequence:format())
        box.schema.sequence.create('test')
        box.schema.create_space('test1')
        box.space.test1:create_index('pk', {sequence = true})
        box.schema.create_space('test2')
        box.space.test2:create_index('pk', {sequence = box.sequence.test.id})
        box.schema.user.create('alice')
        box.session.su('alice', function()
            t.assert_equals(box.space._vspace_sequence:select(), {})
        end)
        box.schema.user.grant('alice', 'read', 'space', 'test1')
        box.session.su('alice', function()
            t.assert_equals(box.space._vspace_sequence:select(), {
                {box.space.test1.id, box.sequence.test1_seq.id, true, 0, ''},
            })
        end)
        box.schema.user.grant('alice', 'read', 'space', 'test2')
        box.session.su('alice', function()
            t.assert_equals(box.space._vspace_sequence:select(), {
                {box.space.test1.id, box.sequence.test1_seq.id, true, 0, ''},
                {box.space.test2.id, box.sequence.test.id, false, 0, ''},
            })
        end)
        box.schema.user.revoke('alice', 'read', 'space', 'test1')
        box.schema.user.revoke('alice', 'read', 'space', 'test2')
        box.session.su('alice', function()
            t.assert_equals(box.space._vspace_sequence:select(), {})
        end)
        box.session.su('admin', box.schema.user.grant,
                       'alice', 'read', 'universe')
        box.session.su('alice', function()
            t.assert_equals(box.space._vspace_sequence:select(), {
                {box.space.test1.id, box.sequence.test1_seq.id, true, 0, ''},
                {box.space.test2.id, box.sequence.test.id, false, 0, ''},
            })
        end)
    end)
end
