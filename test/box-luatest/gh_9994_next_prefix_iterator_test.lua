local server = require('luatest.server')
local t = require('luatest')

local g = t.group()

g.before_all(function(cg)
    cg.server = server:new()
    cg.server:start()
end)

g.after_all(function(cg)
    cg.server:drop()
end)

g.after_each(function(cg)
    cg.server:exec(function()
        if box.space.test then
            box.space.test:drop()
        end
        if box.func.func then
            box.func.func:drop()
        end
    end)
end)

-- Check some cases when NP and PP iterators are unsupported..
g.test_next_prefix_unsupported = function(cg)
    cg.server:exec(function()
        local s = box.schema.space.create('test')
        s:create_index('pk', {type = 'hash', parts = {{1, 'string'}}})
        local m = "Index 'pk' (HASH) of space 'test' (memtx) " ..
                  "does not support requested iterator type"
        t.assert_error_msg_content_equals(m, s.select, s, '', {iterator = 'np'})
        t.assert_error_msg_content_equals(m, s.select, s, '', {iterator = 'pp'})

        s:create_index('sk', {parts = {{1, 'str', collation = 'unicode_ci'}}})
        local i = s.index.sk
        m = "Index 'sk' (TREE) of space 'test' (memtx) " ..
            "does not support requested iterator type along with collation"
        t.assert_error_msg_content_equals(m, i.select, i, '', {iterator = 'np'})
        t.assert_error_msg_content_equals(m, i.select, i, '', {iterator = 'pp'})

        s:drop()
        s = box.schema.space.create('test', {engine = 'vinyl'})
        s:create_index('pk', {parts = {{1, 'string'}}})
        m = "Index 'pk' (TREE) of space 'test' (vinyl) " ..
            "does not support requested iterator type"
        t.assert_error_msg_content_equals(m, s.select, s, '', {iterator = 'np'})
        t.assert_error_msg_content_equals(m, s.select, s, '', {iterator = 'pp'})
        s:drop()
    end)
end

-- Simple test of next prefix and previous prefix iterators.
g.test_next_prefix_simple = function(cg)
    cg.server:exec(function()
        local s = box.schema.space.create('test')
        s:create_index('pk', {parts = {{1, 'string'}}})
        s:replace{'a'}
        s:replace{'aa'}
        s:replace{'ab'}
        s:replace{'b'}
        s:replace{'ba'}
        s:replace{'bb'}
        s:replace{'c'}
        s:replace{'ca'}
        s:replace{'cb'}
        local all = s:select({}, {fullscan = true})
        local rall = s:select({}, {fullscan = true, iterator = 'le'})

        t.assert_equals(s:select({}, {iterator = 'np', fullscan = true}), all)
        t.assert_equals(s:select('', {iterator = 'np'}), {})
        t.assert_equals(s:select('a', {iterator = 'np'}),
                        {{'b'}, {'ba'}, {'bb'}, {'c'}, {'ca'}, {'cb'}})
        t.assert_equals(s:select('b', {iterator = 'np'}),
                        {{'c'}, {'ca'}, {'cb'}})
        t.assert_equals(s:select('c', {iterator = 'np'}), {})
        t.assert_equals(s:select('b', {iterator = 'np', limit = 1}), {{'c'}})

        t.assert_equals(s:select({}, {iterator = 'pp', fullscan = true}), rall)
        t.assert_equals(s:select('', {iterator = 'pp'}), {})
        t.assert_equals(s:select('a', {iterator = 'pp'}), {})
        t.assert_equals(s:select('b', {iterator = 'pp'}),
                        {{'ab'}, {'aa'}, {'a'}})
        t.assert_equals(s:select('c', {iterator = 'pp'}),
                        {{'bb'}, {'ba'}, {'b'}, {'ab'}, {'aa'}, {'a'}})
        t.assert_equals(s:select('b', {iterator = 'pp', limit = 1}), {{'ab'}})

        local function get_pairs(key, opts)
            local res = {}
            for _, t in s:pairs(key, opts) do
                table.insert(res, t)
            end
            return res
        end

        t.assert_equals(get_pairs({}, {iterator = 'np', fullscan = true}), all)
        t.assert_equals(get_pairs('', {iterator = 'np'}), {})
        t.assert_equals(get_pairs('a', {iterator = 'np'}),
                        {{'b'}, {'ba'}, {'bb'}, {'c'}, {'ca'}, {'cb'}})
        t.assert_equals(get_pairs('b', {iterator = 'np'}),
                        {{'c'}, {'ca'}, {'cb'}})
        t.assert_equals(get_pairs('c', {iterator = 'np'}), {})

        t.assert_equals(get_pairs({}, {iterator = 'pp', fullscan = true}), rall)
        t.assert_equals(get_pairs('', {iterator = 'pp'}), {})
        t.assert_equals(get_pairs('a', {iterator = 'pp'}), {})
        t.assert_equals(get_pairs('b', {iterator = 'pp'}),
                        {{'ab'}, {'aa'}, {'a'}})
        t.assert_equals(get_pairs('c', {iterator = 'pp'}),
                        {{'bb'}, {'ba'}, {'b'}, {'ab'}, {'aa'}, {'a'}})
    end)
end

-- Simple test of next prefix and previous prefix iterators with desc order.
g.test_next_prefix_simple_reverse = function(cg)
    cg.server:exec(function()
        local s = box.schema.space.create('test')
        s:create_index('pk', {parts = {{1, 'string', sort_order = 'desc'}}})
        s:replace{'a'}
        s:replace{'aa'}
        s:replace{'ab'}
        s:replace{'b'}
        s:replace{'ba'}
        s:replace{'bb'}
        s:replace{'c'}
        s:replace{'ca'}
        s:replace{'cb'}
        local all = s:select({}, {fullscan = true})
        local rall = s:select({}, {fullscan = true, iterator = 'le'})

        t.assert_equals(s:select({}, {iterator = 'np', fullscan = true}), all)
        t.assert_equals(s:select('', {iterator = 'np'}), {})
        t.assert_equals(s:select('a', {iterator = 'np'}), {})
        t.assert_equals(s:select('b', {iterator = 'np'}),
                        {{'ab'}, {'aa'}, {'a'}})
        t.assert_equals(s:select('c', {iterator = 'np'}),
                        {{'bb'}, {'ba'}, {'b'}, {'ab'}, {'aa'}, {'a'}})
        t.assert_equals(s:select('b', {iterator = 'np', limit = 1}), {{'ab'}})

        t.assert_equals(s:select({}, {iterator = 'pp', fullscan = true}), rall)
        t.assert_equals(s:select('', {iterator = 'pp'}), {})
        t.assert_equals(s:select('a', {iterator = 'pp'}),
                        {{'b'}, {'ba'}, {'bb'}, {'c'}, {'ca'}, {'cb'}})
        t.assert_equals(s:select('b', {iterator = 'pp'}),
                        {{'c'}, {'ca'}, {'cb'}})
        t.assert_equals(s:select('c', {iterator = 'pp'}), {})
        t.assert_equals(s:select('b', {iterator = 'pp', limit = 1}), {{'c'}})

        local function get_pairs(key, opts)
            local res = {}
            for _, t in s:pairs(key, opts) do
                table.insert(res, t)
            end
            return res
        end

        t.assert_equals(get_pairs({}, {iterator = 'np', fullscan = true}), all)
        t.assert_equals(get_pairs('', {iterator = 'np'}), {})
        t.assert_equals(get_pairs('a', {iterator = 'np'}), {})
        t.assert_equals(get_pairs('b', {iterator = 'np'}),
                        {{'ab'}, {'aa'}, {'a'}})
        t.assert_equals(get_pairs('c', {iterator = 'np'}),
                        {{'bb'}, {'ba'}, {'b'}, {'ab'}, {'aa'}, {'a'}})

        t.assert_equals(get_pairs({}, {iterator = 'pp', fullscan = true}), rall)
        t.assert_equals(get_pairs('', {iterator = 'pp'}), {})
        t.assert_equals(get_pairs('a', {iterator = 'pp'}),
                        {{'b'}, {'ba'}, {'bb'}, {'c'}, {'ca'}, {'cb'}})
        t.assert_equals(get_pairs('b', {iterator = 'pp'}),
                        {{'c'}, {'ca'}, {'cb'}})
        t.assert_equals(get_pairs('c', {iterator = 'pp'}), {})
    end)
end

-- Next prefix in scalar index.
g.test_next_prefix_scalar = function(cg)
    cg.server:exec(function()
        local s = box.schema.space.create('test')
        s:create_index('pk', {parts = {{1, 'scalar'}}})
        s:replace{1}
        s:replace{2}
        s:replace{3}
        s:replace{'a'}
        s:replace{'ab'}
        s:replace{'b'}
        s:replace{'ba'}

        t.assert_equals(s:select('a', {iterator = 'np'}), {{'b'}, {'ba'}})
        t.assert_equals(s:select('b', {iterator = 'pp'}),
                        {{'ab'}, {'a'}, {3}, {2}, {1}})
        t.assert_equals(s:select(2, {iterator = 'np'}),
                        {{3}, {'a'}, {'ab'}, {'b'}, {'ba'}})
        t.assert_equals(s:select(2, {iterator = 'pp'}), {{1}})
    end)
end

-- Next prefix in functional index.
g.test_next_prefix_func = function(cg)
    cg.server:exec(function()
        local s = box.schema.space.create('test')
        s:create_index('pk')
        local body = [[
            function(tuple)
                local res = {}
                for _, s in pairs(string.split(tuple[2], ' ')) do
                    table.insert(res, {s})
                end
                return res
            end
        ]]
        box.schema.func.create('func',
                               {body = body,
                                is_deterministic = true,
                                is_sandboxed = true,
                                is_multikey = true})
        local i = s:create_index('sk', {parts = {{1, 'string'}}, func = 'func'})
        s:replace{1, 'a aa ab'}
        s:replace{2, 'b ba bb'}
        s:replace{3, 'c ca cb'}
        t.assert_equals(i:select({}, {iterator = 'np', fullscan = true}),
                        i:select({}, {iterator = 'ge', fullscan = true}))
        t.assert_equals(i:select('', {iterator = 'np'}), {})
        t.assert_equals(i:select('b', {iterator = 'np'}),
                        {{3, 'c ca cb'}, {3, 'c ca cb'}, {3, 'c ca cb'}})
        t.assert_equals(i:select('c', {iterator = 'np'}), {})

        t.assert_equals(i:select({}, {iterator = 'pp', fullscan = true}),
                        i:select({}, {iterator = 'le', fullscan = true}))
        t.assert_equals(i:select('', {iterator = 'pp'}), {})
        t.assert_equals(i:select('b', {iterator = 'pp'}),
                        {{1, 'a aa ab'}, {1, 'a aa ab'}, {1, 'a aa ab'}})
        t.assert_equals(i:select('a', {iterator = 'pp'}), {})
    end)
end

-- Next prefix in json index.
g.test_next_prefix_json = function(cg)
    cg.server:exec(function()
        local s = box.schema.space.create('test')
        s:create_index('pk', {parts = {{1, 'string', path = 'data'}}})
        s:replace{{data = 'a'}}
        s:replace{{data = 'ab'}}
        s:replace{{data = 'b'}}
        s:replace{{data = 'ba'}}

        t.assert_equals(s:select('a', {iterator = 'np'}),
                        {{{data = 'b'}}, {{data = 'ba'}}})
        t.assert_equals(s:select('a', {iterator = 'pp'}), {})
        t.assert_equals(s:select('b', {iterator = 'np'}), {})
        t.assert_equals(s:select('b', {iterator = 'pp'}),
                        {{{data = 'ab'}}, {{data = 'a'}}})
    end)
end

-- Strange but valid cases.
g.test_next_prefix_strange = function(cg)
    cg.server:exec(function()
        local s = box.schema.space.create('test')
        s:create_index('pk', {parts = {{1, 'unsigned'}, {2, 'string'},
                                       {3, 'unsigned'}}})
        local sk = s:create_index('sk', {parts = {{1, 'unsigned'}}})
        s:replace{1, '', 1}
        s:replace{2, 'a', 2}
        s:replace{3, 'b', 3}

        -- Non-string parts works as gt/lt.
        t.assert_equals(s:select({2}, {iterator = 'np'}), {{3, 'b', 3}})
        t.assert_equals(s:select({2}, {iterator = 'pp'}), {{1, '', 1}})
        t.assert_equals(s:select({2, 'a', 2}, {iterator = 'np'}), {{3, 'b', 3}})
        t.assert_equals(s:select({2, 'a', 2}, {iterator = 'pp'}), {{1, '', 1}})
        t.assert_equals(sk:select({2}, {iterator = 'np'}), {{3, 'b', 3}})
        t.assert_equals(sk:select({2}, {iterator = 'pp'}), {{1, '', 1}})

        sk:drop()
        s:replace{1, 'a', 1}
        s:replace{1, 'aa', 1}
        s:replace{1, 'ab', 1}
        s:replace{1, 'b', 1}
        s:replace{1, 'ba', 1}
        s:replace{1, 'bb', 1}

        -- Previous prefix takes all tuples with string.sub('', 1, 3) < 'a'
        t.assert_equals(s:select({1, 'aaa'}, {iterator = 'pp'}),
                        {{1, 'aa', 1}, {1, 'a', 1}, {1, '', 1}})
        s:drop()

        s = box.schema.space.create('test')
        s:create_index('pk', {parts = {{1, 'string'}}})
        s:replace{'\x00'}
        s:replace{'\x00\x00'}
        s:replace{'\x00\x00\x01'}
        s:replace{'\x00\x00\x02'}
        s:replace{'\x7f'}
        s:replace{'\x7f\x7f'}
        s:replace{'\x7f\x7f\x01'}
        s:replace{'\x7f\x7f\x02'}
        s:replace{'\x80'}
        s:replace{'\x80\x80'}
        s:replace{'\x80\x80\x01'}
        s:replace{'\x80\x80\x02'}
        s:replace{'\xff'}
        s:replace{'\xff\xff'}
        s:replace{'\xff\xff\x01'}
        s:replace{'\xff\xff\x02'}

        local opts = {iterator = 'np', limit = 1}
        t.assert_equals(s:select('\x00', opts), {{'\x7f'}})
        t.assert_equals(s:select('\x00\x00', opts), {{'\x7f'}})
        t.assert_equals(s:select('\x7f', opts), {{'\x80'}})
        t.assert_equals(s:select('\x7f\x7f', opts), {{'\x80'}})
        t.assert_equals(s:select('\x80', opts), {{'\xff'}})
        t.assert_equals(s:select('\x80\x80', opts), {{'\xff'}})
        t.assert_equals(s:select('\xff', opts), {})
        t.assert_equals(s:select('\xff\xff', opts), {})

        local opts = {iterator = 'pp', limit = 1}
        t.assert_equals(s:select('\x00', opts), {})
        t.assert_equals(s:select('\x00\x00', opts), {{'\x00'}})
        t.assert_equals(s:select('\x7f', opts), {{'\x00\x00\x02'}})
        t.assert_equals(s:select('\x7f\x7f', opts), {{'\x7f'}})
        t.assert_equals(s:select('\x80', opts), {{'\x7f\x7f\x02'}})
        t.assert_equals(s:select('\x80\x80', opts), {{'\x80'}})
        t.assert_equals(s:select('\xff', opts), {{'\x80\x80\x02'}})
        t.assert_equals(s:select('\xff\xff', opts), {{'\xff'}})
    end)
end

-- Practical case. List all files and directories in given directory.
g.test_directory_list = function(cg)
    cg.server:exec(function()
        local s = box.schema.space.create('test')
        s:format{{'division', 'unsigned'},
                 {'path', 'string'},
                 {'version', 'unsigned'}}
        s:create_index('pk', {parts = {{'division'}, {'path'}, {'version'}}})
        s:replace{0, '/home/', 0}
        s:replace{1, '/home/another_user/file.txt', 0}
        s:replace{1, '/home/who_is_that/file.txt', 0}
        s:replace{1, '/home/user/file.txt', 0}
        s:replace{1, '/home/user/file.txt', 1}
        s:replace{1, '/home/user/data', 0}
        s:replace{1, '/home/user/data1', 0}
        s:replace{1, '/home/user/data2', 0}
        s:replace{1, '/home/user/folder/', 0}
        s:replace{1, '/home/user/folder/subfolder/data.txt', 0}
        s:replace{1, '/home/user/folder1/subfolder/data.txt', 0}
        s:replace{1, '/home/user/bin/tarantool', 1}
        s:replace{1, '/home/user/bin/tarantool_ctl', 1}
        s:replace{1, '/home/user/work/tarantool/src/main.cc', 0}
        s:replace{1, '/home/user/work/tarantool/src/main.cc', 1}
        s:replace{1, '/home/user/work/tarantool/test/prefix_test.lua', 0}
        s:replace{1, '/home/user/work/tarantool/README.md', 0}
        s:replace{1, '/home/user/work/small/README.md', 0}
        s:replace{1, '/home/user/work/folder/', 0}
        s:replace{1, '/home/user/work/tarantool/folder/', 0}
        s:replace{2, '/home/user/secret.txt', 0}

        local function list(division, path)
            local res = {}
            if not string.endswith(path, '/') then
                path = path .. '/'
            end
            local function is_good(t)
                return t and t.division == division and
                       string.startswith(t.path, path)
            end
            local function get_name(subpath)
                local name = string.sub(subpath, #path + 1)
                local pos = string.find(name, '/')
                if pos then
                    name = string.sub(name, 1, pos)
                end
                return name
            end

            local opts = {iterator = 'gt', limit = 1}
            local t = s:select({division, path}, opts)[1]
            if not is_good(t) then
                return res
            end
            local name = get_name(t.path)
            table.insert(res, name)
            while true do
                opts.iterator = string.endswith(name, '/') and 'np' or 'gt'
                t = s:select({division, path .. name}, opts)[1]
                if not is_good(t) then
                    return res
                end
                name = get_name(t.path)
                table.insert(res, name)
            end
        end

        t.assert_equals(list(1, '/home/user'),
                        {'bin/', 'data', 'data1', 'data2',
                         'file.txt', 'folder/', 'folder1/', 'work/'})
        t.assert_equals(list(1, '/home/user/bin/'),
                        {'tarantool', 'tarantool_ctl'})
        t.assert_equals(list(1, '/home/user/work/'),
                        {'folder/', 'small/', 'tarantool/'})
        t.assert_equals(list(1, '/home/user/work/tarantool'),
                        {'README.md', 'folder/', 'src/', 'test/'})
    end)
end
