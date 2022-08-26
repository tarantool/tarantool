local server = require('test.luatest_helpers.server')
local t = require('luatest')
local tree_g = t.group('Tree index tests')

tree_g.before_all(function()
    tree_g.server = server:new{
        alias   = 'default',
    }
    tree_g.server:start()
end)

tree_g.after_all(function()
    tree_g.server:drop()
end)

tree_g.before_each(function()
    tree_g.server:exec(function()
        box.schema.space.create('s', {engine = 'memtx'})
    end)
end)

tree_g.after_each(function()
    tree_g.server:exec(function()
        box.space.s:drop()
    end)
end)

tree_g.test_tree_pagination = function()
    tree_g.server:exec(function()
        local t = require('luatest')
        local s = box.space.s
        s:create_index("pk", {type = "tree"})
        s:create_index("sk", {parts = {2}, type = "tree", unique=false})

        -- Fetch position in empty space
        local tuples, pos = s:select(nil,
                {limit=2, fullscan=true, fetch_pos=true})
        t.assert_equals(tuples, {})
        t.assert_equals(pos, "")

        for i = 1, 11 do
            s:replace{i, 1}
        end

        local tuples1 = nil
        local tuples2 = nil
        local tuples_offset = nil
        local pos = ""
        local last_tuple = box.NULL
        local last_pos = nil

        -- Test fullscan pagination
        for i = 0, 5 do
            tuples1, pos = s:select(nil,
                    {limit=2, fullscan=true, fetch_pos=true, after=pos})
            tuples2 = s:select(nil,
                    {limit=2, fullscan=true, after=last_tuple})
            last_tuple = tuples2[2]
            tuples_offset = s:select(nil,
                    {limit=2, fullscan=true, offset=i*2})
            t.assert_equals(tuples1, tuples_offset)
            t.assert_equals(tuples2, tuples_offset)
        end
        tuples1, last_pos = s:select(nil,
                {limit=2, fullscan=true, fetch_pos=true, after=pos})
        t.assert_equals(tuples1, {})
        t.assert_equals(last_pos, pos)

        -- Test pagination on range iterators
        local key_iter = {
            [3] = 'GE',
            [2] = 'GT',
            [9] = 'LE',
            [10] = 'LT',
        }
        for key, iter in pairs(key_iter) do
            tuples1 = nil
            tuples2 = nil
            tuples_offset = nil
            pos = ""
            last_tuple = box.NULL
            last_pos = nil
            for i = 0, 4 do
                tuples1, pos = s:select(key,
                        {limit=2, iterator=iter, fetch_pos=true, after=pos})
                tuples2 = s:select(key,
                        {limit=2, iterator=iter, after=last_tuple})
                last_tuple = tuples2[2]
                tuples_offset = s:select(key,
                        {limit=2, iterator=iter, offset=i*2})
                t.assert_equals(tuples1, tuples_offset)
                t.assert_equals(tuples2, tuples_offset)
            end
            tuples1, last_pos = s:select(key,
                    {limit=2, iterator=iter, fetch_pos=true, after=pos})
            t.assert_equals(tuples1, {})
            t.assert_equals(last_pos, pos)
        end

        -- Test pagination on equality iterators
        s:replace{0, 0}
        s:replace{12, 2}
        for _, iter in pairs({'EQ', 'REQ'}) do
            tuples1 = nil
            tuples2 = nil
            tuples_offset = nil
            pos = ""
            last_tuple = box.NULL
            last_pos = nil
            for i = 0, 5 do
                tuples1, pos = s.index.sk:select(1,
                        {limit=2, iterator=iter, fetch_pos=true, after=pos})
                tuples2 = s.index.sk:select(1,
                        {limit=2, iterator=iter, after=last_tuple})
                last_tuple = tuples2[2]
                tuples_offset = s.index.sk:select(1,
                        {limit=2, iterator=iter, offset=i*2})
                t.assert_equals(tuples1, tuples_offset)
                t.assert_equals(tuples2, tuples_offset)
            end
            tuples, last_pos = s.index.sk:select(1,
                    {iterator=iter, limit=2, fetch_pos=true, after=pos})
            t.assert_equals(tuples, {})
            t.assert_equals(last_pos, pos)
        end
    end)
end

tree_g.test_tree_multikey_pagination = function()
    tree_g.server:exec(function()
        local t = require('luatest')
        local s = box.space.s
        s:create_index("pk", {type = "tree"})
        local sk = s:create_index("sk",
                {parts = {{field = 2, type = 'uint', path = '[*]'}},
                 type = "tree", unique=false})

        -- Fetch position in empty space
        local tuples, pos = sk:select(nil,
                {limit=2, fullscan=true, fetch_pos=true})
        t.assert_equals(tuples, {})
        t.assert_equals(pos, "")

        for i = 1, 3 do
            s:replace{i, {1, 2, 3}}
        end

        local tuples = nil
        local tuples_offset = nil
        local pos = ""
        local last_pos = nil

        -- Test fullscan pagination
        for i = 0, 4 do
            tuples, pos = sk:select(nil,
                    {limit=2, fullscan=true, fetch_pos=true, after=pos})
            tuples_offset = sk:select(nil,
                    {limit=2, fullscan=true, offset=i*2})
            t.assert_equals(tuples, tuples_offset)
        end
        tuples, last_pos = sk:select(nil,
                {limit=2, fullscan=true, fetch_pos=true, after=pos})
        t.assert_equals(tuples, {})
        t.assert_equals(last_pos, pos)

        -- Test pagination on range iterators
        local key_iter = {
            [2] = 'GE',
            [1] = 'GT',
            [2] = 'LE',
            [3] = 'LT',
        }
        for key, iter in pairs(key_iter) do
            tuples = nil
            tuples_offset = nil
            pos = ""
            last_pos = nil
            for i = 0, 2 do
                tuples, pos = sk:select(key,
                        {limit=2, iterator=iter, fetch_pos=true, after=pos})
                tuples_offset = sk:select(key,
                        {limit=2, iterator=iter, offset=i*2})
                t.assert_equals(tuples, tuples_offset)
            end
            tuples, last_pos = sk:select(key,
                    {limit=2, iterator=iter, fetch_pos=true, after=pos})
            t.assert_equals(tuples, {})
            t.assert_equals(last_pos, pos)
        end

        -- Test pagination on equality iterators
        for _, iter in pairs({'EQ', 'REQ'}) do
            tuples = nil
            tuples_offset = nil
            pos = ""
            last_pos = nil
            for i = 0, 1 do
                tuples, pos = sk:select(2,
                        {limit=2, iterator=iter, fetch_pos=true, after=pos})
                tuples_offset = sk:select(2,
                        {limit=2, iterator=iter, offset=i*2})
                t.assert_equals(tuples, tuples_offset)
            end
            tuples, last_pos = sk:select(2,
                    {iterator=iter, limit=2, fetch_pos=true, after=pos})
            t.assert_equals(tuples, {})
            t.assert_equals(last_pos, pos)
        end

        -- Test that after with tuple in multikey index returns an error
        local tuple = s.index.pk:get(1)
        t.assert_error_msg_contains(
                "pagination without iteration context in multikey index",
                sk.select, sk, nil, {fullscan=true, after=tuple})
    end)
end

--[[ We must return an empty position if there are no tuples
satisfying the filters. ]]--
tree_g.test_no_tuples_satisfying_filters = function()
    tree_g.server:exec(function()
        local t = require('luatest')
        local s = box.space.s
        s:create_index("pk", {type = "tree"})
        s:create_index("sk",
                {parts =
                 {{field = 2, type = 'uint', path = '[*]'}},
                 type = "tree",
                 unique=false})

        local tuples = nil
        local pos = ""

        s:replace{1, {1, 2}}

        tuples, pos = s:select(3, {limit=1, iterator='GE', fetch_pos=true})
        t.assert_equals(pos, "")
        s:replace{2, {1, 2}}
        s:replace{3, {1, 2}}
        s:replace{4, {1, 2}}
        tuples = s:select(3, {limit=1, iterator='GE', after=pos})
        t.assert_equals(tuples[1], {3, {1, 2}})

        tuples, pos = s.index.sk:select(4, {limit=1, iterator='GE', fetch_pos=true})
        t.assert_equals(pos, "")
        s:replace{5, {3, 4}}
        tuples = s.index.sk:select(4, {limit=1, iterator='GE', after=pos})
        t.assert_equals(tuples[1], {5, {3, 4}})
    end)
end

tree_g.test_invalid_positions = function()
    tree_g.server:exec(function()
        local t = require('luatest')
        local s = box.space.s
        s:create_index('pk', {type = 'tree'})
        s:create_index('sk',
                {parts = {{field = 2, type = 'string'}}, type = 'tree'})
        s:replace{1, 'Zero'}

        local tuples = nil
        local pos = {1}
        local flag, err = pcall(function()
            s:select(1, {limit=1, iterator='GE', after=pos})
        end)
        t.assert_equals(flag, false)
        t.assert_equals(err.code, box.error.INVALID_POSITION)

        pos = "abcd"
        flag, err = pcall(function()
            s:select(1, {limit=1, iterator='GE', after=pos})
        end)
        t.assert_equals(flag, false)
        t.assert_equals(err.code, box.error.INVALID_POSITION)

        tuples, pos = s:select(nil, {fullscan=true, limit=1, fetch_pos=true})
        t.assert(#pos > 0)
        flag, err = pcall(function()
            s.index.sk:select(nil, {fullscan=true, limit=1, after=pos})
        end)
        t.assert_equals(flag, false)
        t.assert_equals(err.code, box.error.INVALID_POSITION)
    end)
end

tree_g.test_tuple_pos_no_duplicates = function()
    tree_g.server:exec(function()
        local t = require('luatest')
        local s = box.space.s
        s:create_index("pk", {type = "tree"})

        for i = 1, 4 do
            s:replace{i}
        end
        for i = 6, 10 do
            s:replace{i}
        end

        local tuples, pos = s:select(1, {iterator='GE', fetch_pos=true, limit=5})
        s:replace{5}
        tuples = s:select(1, {iterator='GE', after=pos})
        t.assert_equals(#tuples, 4)
        t.assert_equals(tuples, s:select(7, {iterator='GE'}))

        tuples, pos = s:select(1, {iterator='GE', fetch_pos=true})
        s:delete(10)
        tuples, pos = s:select(1, {iterator='GE', fetch_pos=true, after=pos})
        s:replace{10}
        s:replace{11}
        tuples, pos = s:select(1, {iterator='GE', fetch_pos=true, after=pos})
        t.assert_equals(#tuples, 1)
        t.assert_equals(tuples[1], {11})
    end)
end

tree_g.test_tuple_pos_simple = function()
    tree_g.server:exec(function()
        local t = require('luatest')
        local s = box.space.s
        s:create_index("pk", {type = "tree"})
        s:create_index("sk", {parts = {{field = 2, type = 'uint', path = '[*]'}}, type = "tree", unique=false})

        local tuples = nil
        local pos = ""
        local last_pos = nil

        for i = 1, 10 do
            s:replace{i, {1, 2}}
        end

        pos = ""
        for i = 0, 4 do
            tuples, last_pos = s:select(nil,
                    {limit=2, fullscan=true, fetch_pos=true, after=pos})
            t.assert_equals(tuples[1][1], i * 2 + 1)
            t.assert_equals(tuples[2][1], i * 2 + 2)
            t.assert_equals(tuples[3], nil)
            pos = s.index.pk:tuple_pos(tuples[2])
            t.assert_equals(pos, last_pos)
        end
        tuples, last_pos = s:select(nil,
                {limit=2, fullscan=true, fetch_pos=true, after=pos})
        t.assert_equals(tuples, {})
        t.assert_equals(last_pos, pos)

        tuples = s:select(1)
        t.assert_error_msg_contains(
                "pagination without iteration context in multikey index",
                s.index.sk.tuple_pos, s.index.sk, tuples[1])
    end)
end

tree_g.test_tuple_pos_invalid_tuple = function()
    tree_g.server:exec(function()
        local t = require('luatest')
        local s = box.space.s
        s:create_index("pk", {type = "tree"})

        local tuples = nil
        local pos = ""
        local last_pos = nil

        for i = 1, 10 do
            s:replace{i, 0}
        end

        local err_msg = "Usage index.tuple_pos(space_id, index_id, tuple)"

        t.assert_error_msg_contains(err_msg, s.index.pk.tuple_pos, s.index.pk)
        t.assert_error_msg_contains(err_msg, s.index.pk.tuple_pos, s.index.pk,
                                    {1, 0})
    end)
end


local no_sup = t.group('Unsupported pagination', {
            {engine = 'memtx', type = 'hash'},
            {engine = 'memtx', type = 'bitset'},
            {engine = 'memtx', type = 'rtree'},
})

no_sup.before_all(function(cg)
    cg.server = server:new{
        alias   = 'default',
    }
    cg.server:start()
end)

no_sup.after_all(function(cg)
    cg.server:drop()
end)

no_sup.before_each(function(cg)
    cg.server:exec(function(engine, type)
        local s = box.schema.space.create('test', {engine=engine})
        s:create_index('pk')
        s:create_index('sk', {type=type})
        -- An error will not be thrown if space not tuples found.
        if type == 'rtree' then
            s:replace{0, {0, 0}}
        else
            s:replace{0, 0}
        end
    end, {cg.params.engine, cg.params.type})
end)

no_sup.after_each(function(cg)
    cg.server:exec(function()
        box.space.test:drop()
    end)
end)

no_sup.test_unsupported_pagination = function(cg)
    cg.server:exec(function()
        local t = require('luatest')
        t.assert_error_msg_contains('does not support pagination',
                box.space.test.index.sk.select, box.space.test.index.sk,
                nil, {fullscan=true, fetch_pos=true})
    end)
end
