local server = require('luatest.server')
local t = require('luatest')

local g = t.group('gh_7858_net_box_space_sequence')

g.before_all(function(cg)
    cg.server = server:new({alias = 'default'})
    cg.server:start()
end)

g.after_all(function(cg)
    cg.server:drop()
end)

g.after_test('test_net_box_space_sequence', function(cg)
    cg.server:exec(function()
        if box.space.test1 ~= nil then
            box.space.test1:drop()
        end
        if box.space.test2 ~= nil then
            box.space.test2:drop()
        end
        if box.space.test3 ~= nil then
            box.space.test3:drop()
        end
        if box.sequence.test ~= nil then
            box.sequence.test:drop()
        end
    end)
end)

g.test_net_box_space_sequence = function(cg)
    cg.server:exec(function()
        local t = require('luatest')
        local net = require('net.box')
        box.schema.sequence.create('test')
        box.schema.create_space('test1')
        box.space.test1:create_index('pk')
        box.space.test1:create_index('sk', {parts = {2, 'unsigned'}})
        box.schema.create_space('test2')
        box.space.test2:create_index('pk', {sequence = true})
        box.space.test2:create_index('sk', {parts = {2, 'unsigned'}})
        box.schema.create_space('test3')
        box.space.test3:create_index('pk', {
            parts = {{'[2].foo.bar', 'unsigned'}},
            sequence = box.sequence.test.id,
        })
        box.space.test3:create_index('sk', {parts = {1, 'unsigned'}})
        local conn = net.connect(box.cfg.listen)
        t.assert_equals(conn.space.test1.index.pk.sequence_id, nil)
        t.assert_equals(conn.space.test1.index.pk.sequence_fieldno, nil)
        t.assert_equals(conn.space.test1.index.pk.sequence_path, nil)
        t.assert_equals(conn.space.test1.index.sk.sequence_id, nil)
        t.assert_equals(conn.space.test1.index.sk.sequence_fieldno, nil)
        t.assert_equals(conn.space.test1.index.sk.sequence_path, nil)
        t.assert_equals(conn.space.test2.index.pk.sequence_id,
                        box.sequence.test2_seq.id)
        t.assert_equals(conn.space.test2.index.pk.sequence_fieldno, 1)
        t.assert_equals(conn.space.test2.index.pk.sequence_path, nil)
        t.assert_equals(conn.space.test2.index.sk.sequence_id, nil)
        t.assert_equals(conn.space.test2.index.sk.sequence_fieldno, nil)
        t.assert_equals(conn.space.test2.index.sk.sequence_path, nil)
        t.assert_equals(conn.space.test3.index.pk.sequence_id,
                        box.sequence.test.id)
        t.assert_equals(conn.space.test3.index.pk.sequence_fieldno, 2)
        t.assert_equals(conn.space.test3.index.pk.sequence_path, '.foo.bar')
        t.assert_equals(conn.space.test3.index.sk.sequence_id, nil)
        t.assert_equals(conn.space.test3.index.sk.sequence_fieldno, nil)
        t.assert_equals(conn.space.test3.index.sk.sequence_path, nil)
        conn:close()
    end)
end

local g_old_schema = t.group('gh_7858_net_box_space_sequence.old_schema')

g_old_schema.before_test('test_net_box_space_sequence', function(cg)
    cg.server = server:new({
        alias = 'default',
        datadir = 'test/box-luatest/upgrade/2.10.4',
    })
    cg.server:start()
end)

g_old_schema.after_test('test_net_box_space_sequence', function(cg)
    cg.server:drop()
end)

g_old_schema.test_net_box_space_sequence = function(cg)
    cg.server:exec(function()
        local t = require('luatest')
        local net = require('net.box')
        t.assert_not_equals(box.space.gh_7858, nil)
        t.assert_not_equals(box.sequence.gh_7858_seq, nil)
        t.assert_equals(box.space.gh_7858.index.pk.sequence_id,
                        box.sequence.gh_7858_seq.id)
        t.assert_equals(box.space.gh_7858.index.pk.sequence_fieldno, 2)
        t.assert_equals(box.space.gh_7858.index.pk.sequence_path, '[3]')
        local conn = net.connect(box.cfg.listen)
        t.assert_equals(conn.error, nil)
        t.assert_not_equals(conn.space.gh_7858, nil)
        t.assert_equals(conn.space.gh_7858.index.pk.sequence_id, nil)
        t.assert_equals(conn.space.gh_7858.index.pk.sequence_fieldno, nil)
        t.assert_equals(conn.space.gh_7858.index.pk.sequence_path, nil)
        box.schema.upgrade()
        t.assert_equals(conn:ping(), true)
        t.assert_equals(conn.space.gh_7858.index.pk.sequence_id,
                        box.sequence.gh_7858_seq.id)
        t.assert_equals(conn.space.gh_7858.index.pk.sequence_fieldno, 2)
        t.assert_equals(conn.space.gh_7858.index.pk.sequence_path, '[3]')
        conn:close()
    end)
end
