local common = require('test.vinyl-luatest.common')
local server = require('luatest.server')
local t = require('luatest')

local g = t.group()

g.before_all(function()
    g.server = server:new({
        alias = 'master',
        box_cfg = common.default_box_cfg(),
    })
    g.server:start()
end)

g.after_all(function()
    g.server:drop()
end)

g.after_each(function()
    g.server:exec(function()
        local s = box.space.test
        if s then
            s:drop()
        end
    end)
end)

g.test_option = function()
    g.server:exec(function()
        local s

        -- Invalid arguments.
        t.assert_error_msg_content_equals(
            "Incorrect value for option 'vinyl_defer_deletes': " ..
            "should be of type boolean",
            box.cfg, {vinyl_defer_deletes = "foo"})
        t.assert_error_msg_content_equals(
            "Illegal parameters, options parameter 'defer_deletes' " ..
            "should be of type boolean",
            box.schema.space.create, 'test',
            {engine = 'vinyl', defer_deletes = "foo"})
        s = box.schema.space.create('test', {engine = 'vinyl'})
        t.assert_error_msg_content_equals(
            "Illegal parameters, options parameter 'defer_deletes' " ..
            "should be of type boolean",
            s.alter, s, {defer_deletes = "foo"})
        s:drop()

        -- Space create, box.cfg.vinyl_defer_deletes = false.
        box.cfg({vinyl_defer_deletes = false})
        s = box.schema.space.create('test', {engine = 'vinyl'})
        t.assert_equals(box.space.test.defer_deletes, false)
        t.assert_equals(box.space._space:get(s.id).flags.defer_deletes, nil)
        s:drop()
        s = box.schema.space.create('test', {
            engine = 'vinyl', defer_deletes = true})
        t.assert_equals(box.space.test.defer_deletes, true)
        t.assert_equals(box.space._space:get(s.id).flags.defer_deletes, true)
        s:drop()

        -- Space create, box.cfg.vinyl_defer_deletes = true.
        box.cfg({vinyl_defer_deletes = true})
        s = box.schema.space.create('test', {engine = 'vinyl'})
        t.assert_equals(box.space.test.defer_deletes, true)
        t.assert_equals(box.space._space:get(s.id).flags.defer_deletes, true)
        s:drop()
        s = box.schema.space.create('test', {
            engine = 'vinyl', defer_deletes = false})
        t.assert_equals(box.space.test.defer_deletes, false)
        t.assert_equals(box.space._space:get(s.id).flags.defer_deletes, nil)
        s:drop()

        -- Space alter.
        s = box.schema.space.create('test', {
            engine = 'vinyl', defer_deletes = false})
        s:alter({defer_deletes = true})
        t.assert_equals(box.space.test.defer_deletes, true)
        t.assert_equals(box.space._space:get(s.id).flags.defer_deletes, true)
        s:alter({defer_deletes = false})
        t.assert_equals(box.space.test.defer_deletes, false)
        t.assert_equals(box.space._space:get(s.id).flags.defer_deletes, false)
        s:drop()

        -- Memtx space.
        box.cfg({vinyl_defer_deletes = true})
        s = box.schema.space.create('test')
        t.assert_equals(box.space.test.defer_deletes, nil)
        t.assert_equals(box.space._space:get(s.id).flags.defer_deletes, nil)
    end)
end

g.test_disabled = function()
    g.server:exec(function()
        local s = box.schema.space.create('test', {
            engine = 'vinyl', defer_deletes = false})
        s:create_index('pk', {parts = {1, 'unsigned'}})
        s:create_index('sk', {parts = {2, 'unsigned'}, unique = false})
        s:replace({1, 1})
        t.assert_equals(s.index.pk:stat().lookup, 1)
        s:delete(1)
        t.assert_equals(s.index.pk:stat().lookup, 2)
        s:drop()
    end)
end

g.test_enabled = function()
    g.server:exec(function()
        local s = box.schema.space.create('test', {
            engine = 'vinyl', defer_deletes = true})
        s:create_index('pk', {parts = {1, 'unsigned'}})
        s:create_index('sk', {parts = {2, 'unsigned'}, unique = false})
        s:replace({1, 1})
        t.assert_equals(s.index.pk:stat().lookup, 0)
        s:delete(1)
        t.assert_equals(s.index.pk:stat().lookup, 0)
        s:drop()
    end)
end
