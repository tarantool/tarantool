local server = require('luatest.server')
local t = require('luatest')

local g = t.group('rtree-neighbor-pagination')

g.before_all(function()
    g.server = server:new()
    g.server:start()
end)

g.after_all(function()
    g.server:stop()
end)

g.test_rtree_pagination_neighbor = function()
    g.server:exec(function()
        local ffi = require('ffi')

        local s = box.schema.space.create('test_rect')
        s:format({ { type = 'number', name = 'id' },
            { type = 'array', name = 'content' } })
        local hash_index = s:create_index('primary',
            { type = 'tree', parts = {'id'} })
        local rtree_index = s:create_index('rtree_ind',
            { type = 'rtree', unique = false,
             parts = {'content'} })

        for i = 1, 10 do
            for j = 1, 10 do
                s:insert{i * 10 + j,
                    {i, j, i + 1, j + 1}}
            end
        end
        -- [[11,[1,1,2,2]],
        -- [12,[1,2,2,3]],
        -- [21,[2,1,3,2]],
        -- [22,[2,2,3,3]],
        -- [13,[1,3,2,4]],
        -- [31,[3,1,4,2]],
        -- [23,[2,3,3,4]],
        -- [32,[3,2,4,3]],
        -- [33,[3,3,4,4]]]

        local tuples
        local pos
        hash_index:select{0};

        tuples, pos = rtree_index:select({1, 1}, 
            {iterator = 'neighbor',
            fetch_pos=true, limit=4})

        t.assert_equals(tuples[1],
            {11, {1, 1, 2, 2}})

        t.assert_equals(tuples,
            {{11, {1, 1, 2, 2}}, {12, {1, 2, 2, 3}},
             {21, {2, 1, 3, 2}}, {22, {2, 2, 3, 3}}})

        tuples, pos = rtree_index:select({1, 1},
            {iterator = 'neighbor', fetch_pos=true, 
            limit=3, after=pos})

        t.assert_equals(tuples,
            {{13, {1, 3, 2, 4}}, {31, {3, 1, 4, 2}},
             {23, {2, 3, 3, 4}}})

        tuples = rtree_index:select({1, 1},
            {iterator = 'neighbor',
            fetch_pos=false, limit=2, after=pos})
        t.assert_equals(tuples,
            {{32, {3, 2, 4, 3}}, {33, {3, 3, 4, 4}}})
    end)
end

g.test_rtree_pagination_neighbor_points = function()
    g.server:exec(function()
        local ffi = require('ffi')

        local s = box.schema.space.create('test_points')
        s:format({ { type = 'number', name = 'id' },
            { type = 'array', name = 'content' } })
        local hash_index = s:create_index('primary',
            { type = 'tree', parts = {'id'} })
        local rtree_index = s:create_index('rtree_ind',
            { type = 'rtree', unique = false,
             parts = {'content'} })

        for i = 2, 6, 2 do
            for j = 1, 10 do
                s:insert{i * 10 + j, {i, j + 3}}
            end
        end
        -- [[21,[2,4],
        -- [22,[2,5]],
        -- [41,[4,4]],
        -- [42,[4,5]],
        -- [23,[2,6]],
        -- [61,[6,4]],
        -- [43,[4,6]],
        -- [24,[2,7]],
        -- [62,[6,5]],
        -- [44,[4,7]],
        -- [63,[6,6]],
        -- [25,[2,8]],
        -- [45,[4,8]],
        -- [64,[6,7]]

        local tuples
        local pos
        hash_index:select{0};

        tuples, pos = rtree_index:select({1, 1},
            {iterator = 'neighbor',
            fetch_pos=true, limit=4})

        t.assert_equals(tuples[1], {21, {2, 4}})

        t.assert_equals(tuples,
            {{21, {2, 4}}, {22, {2, 5}},
             {41, {4, 4}}, {42, {4, 5}}})

        tuples, pos = rtree_index:select({1, 1},
            {iterator = 'neighbor',
            fetch_pos=true, limit=4, after=pos})

        t.assert_equals(tuples,
            {{23, {2, 6}}, {61, {6, 4}},
             {43, {4, 6}}, {24, {2, 7}}})

        tuples, pos = rtree_index:select({1, 1},
            {iterator = 'neighbor',
            fetch_pos=false, limit=3, after=pos})
        t.assert_equals(tuples,
            {{62, {6, 5}}, {44, {4, 7}},
             {63, {6, 6}}})

        tuples, pos = rtree_index:select({1, 1},
            {iterator = 'neighbor',
            fetch_pos=true, limit=0, after=pos})
        t.assert_equals(tuples, {})
        t.assert_equals(pos, nil)
    end)
end

g.test_rtree_pagination_neighbor_points = function()
    g.server:exec(function()
        local ffi = require('ffi')

        local function double(n)
            return ffi.cast('double', n)
        end

        local s = box.schema.space.create(
            'test_negative_coords')
        s:format({ { type = 'number', name = 'id' },
            { type = 'array', name = 'content' } })
        local hash_index = s:create_index('primary',
            { type = 'tree', parts = {'id'} })
        local rtree_index = s:create_index('rtree_ind',
            { type = 'rtree', unique = false,
            parts = {'content'} })

        for i = 2, 6, 2 do
            for j = 1, 10 do
                s:insert{i * 10 + j,
                    {i, j + 3, -i, -j * 2}}
            end
        end
    
        local tuples
        local pos
        hash_index:select{0};

        tuples, pos = rtree_index:select({1, 0},
            {iterator = 'neighbor',
            fetch_pos=true, limit=4})

        t.assert_equals(tuples,
            {{21,{2,4,-2,-2}}, {22,{2,5,-2,-4}},
             {23,{2,6,-2,-6}}, {24,{2,7,-2,-8}}})

        tuples, pos = rtree_index:select({1, 0},
            {iterator = 'neighbor',
            fetch_pos=true, limit=4, after=pos})

        t.assert_equals(tuples,
            {{41,{4,4,-4,-2}}, {42,{4,5,-4,-4}},
             {43,{4,6,-4,-6}}, {44,{4,7,-4,-8}}})

        tuples, pos = rtree_index:select({1, 0},
            {iterator = 'neighbor',
            fetch_pos=false, limit=3, after=pos})

        t.assert_equals(tuples,
            {{61,{6,4,-6,-2}}, {62,{6,5,-6,-4}},
             {63,{6,6,-6,-6}}})

        tuples, pos = rtree_index:select({1, 0},
            {iterator = 'neighbor',
            fetch_pos=true, limit=0, after=pos})

        t.assert_equals(tuples, {})
        t.assert_equals(pos, nil)
    end)
end

g.test_rtree_pagination_neighbor_3d_space = function()
    g.server:exec(function()
        local ffi = require('ffi')

        local s = box.schema.space.create(
            'test_3d_space')

        s:format({ { type = 'number', name = 'id' },
            { type = 'array', name = 'content' } })
        local hash_index = s:create_index('primary',
            { type = 'tree', parts = {'id'} })
        local rtree_index = s:create_index('spatial',
            { type = 'rtree', unique = false,
                dimension = 3, parts = {'content'}})

        for k = 5, 10, 2 do
            for i = 2, 8, 2 do
                for j = 1, 10 do
                    s:insert{i * 10 + j + k * 100,
                        {i, j, k, j + 1, i + j, -k * 2}}
                end
            end
        end

        local tuples
        local pos
        hash_index:select{0};

        tuples, pos = rtree_index:select({1, 1, 0, 1, 1, 0},
            {iterator = 'neighbor', fetch_pos=true,
             limit=4})

        t.assert_equals(tuples,
            {{561,{6,1,5,2,7,-10}},{581,{8,1,5,2,9,-10}},
             {741,{4,1,7,2,5,-14}},{761,{6,1,7,2,7,-14}}})

        tuples, pos = rtree_index:select({1, 1, 0, 1, 1, 0},
            {iterator = 'neighbor', fetch_pos=true,
             limit=4, after=pos})

        t.assert_equals(tuples,
            {{781,{8,1,7,2,9,-14}},{541,{4,1,5,2,5,-10}},
             {941,{4,1,9,2,5,-18}},{961,{6,1,9,2,7,-18}}})

        tuples, pos = rtree_index:select({1, 1, 0, 1, 1, 0},
            {iterator = 'neighbor', fetch_pos=false,
             limit=3, after=pos})

        t.assert_equals(tuples,
            {{981,{8,1,9,2,9,-18}},{921,{2,1,9,2,3,-18}},
             {521,{2,1,5,2,3,-10}}})

        tuples, pos = rtree_index:select({1, 1, 0, 1, 1, 0},
            {iterator = 'neighbor', fetch_pos=true,
             limit=0, after=pos})

        t.assert_equals(tuples, {})
        t.assert_equals(pos, nil)
    end)
end