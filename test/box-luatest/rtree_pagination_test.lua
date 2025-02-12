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
            {{23, {2, 6}}, {43, {4, 6}},
            {61, {6, 4}}, {24, {2, 7}}})

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

g.test_rtree_pagination_negative_coords = function()
    g.server:exec(function()

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
            {{41,{4,4,-4,-2}}, {61,{6,4,-6,-2}},
             {42,{4,5,-4,-4}}, {43,{4,6,-4,-6}}})

        tuples, pos = rtree_index:select({1, 0},
            {iterator = 'neighbor',
            fetch_pos=false, limit=3, after=pos})

        t.assert_equals(tuples,
            {{44,{4,7,-4,-8}}, {62,{6,5,-6,-4}},
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
             {542,{4,2,5,3,6,-10}},{562,{6,2,5,3,8,-10}}})

        tuples, pos = rtree_index:select({1, 1, 0, 1, 1, 0},
            {iterator = 'neighbor', fetch_pos=true,
             limit=4, after=pos})

        t.assert_equals(tuples,
            {{582,{8,2,5,3,10,-10}},{524,{2,4,5,5,6,-10}},
             {543,{4,3,5,4,7,-10}},{563,{6,3,5,4,9,-10}}})

        tuples, pos = rtree_index:select({1, 1, 0, 1, 1, 0},
            {iterator = 'neighbor', fetch_pos=false,
             limit=3, after=pos})

        t.assert_equals(tuples,
            {{583,{8,3,5,4,11,-10}},{525,{2,5,5,6,7,-10}},
             {544,{4,4,5,5,8,-10}}})

        tuples, pos = rtree_index:select({1, 1, 0, 1, 1, 0},
            {iterator = 'neighbor', fetch_pos=true,
             limit=0, after=pos})

        t.assert_equals(tuples, {})
        t.assert_equals(pos, nil)
    end)
end

g.test_rtree_pagination_neighbor_4d_space = function()
    g.server:exec(function()

        local s = box.schema.space.create(
            'test_4d_space')

        s:format({ { type = 'number', name = 'id' },
            { type = 'array', name = 'content' } })
        local hash_index = s:create_index('primary',
            { type = 'tree', parts = {'id'} })
        local rtree_index = s:create_index('spatial',
            { type = 'rtree', unique = false,
                dimension = 4, parts = {'content'}})

        for n = 1, 5 do
            for k = 5, 10, 2 do
                for i = 2, 8, 2 do
                    for j = 1, 10 do
                        s:insert{i*10 + j + k*100 + n*1000,
                        {i, j, k, n, j + 1, i + j,
                         -k * 2, n + 1}}
                    end
                end
            end
        end
        local tuples
        local pos
        hash_index:select{0};

        tuples, pos = rtree_index:select({0, 0, 0, 0},
            {iterator = 'neighbor', fetch_pos=true, limit=4})

        t.assert_equals(tuples,
          {{1541,{4,1,5,1,2,5,-10,2}},{1561,{6,1,5,1,2,7,-10,2}},
           {1581,{8,1,5,1,2,9,-10,2}},{1522,{2,2,5,1,3,4,-10,2}}})

        tuples, pos = rtree_index:select({0, 0, 0, 0},
           {iterator = 'neighbor', fetch_pos=true,
            limit=4, after=pos})

        t.assert_equals(tuples,
          {{2521,{2,1,5,2,2,3,-10,3}},{2541,{4,1,5,2,2,5,-10,3}},
           {2561,{6,1,5,2,2,7,-10,3}},{2581,{8,1,5,2,2,9,-10,3}}})

        tuples, pos = rtree_index:select({0, 0, 0, 0},
           {iterator = 'neighbor', fetch_pos=true,
            limit=3, after=pos})

        t.assert_equals(tuples,
           {{2522,{2,2,5,2,3,4,-10,3}},{1562,{6,2,5,1,3,8,-10,2}},
            {3521,{2,1,5,3,2,3,-10,4}}})

        tuples = rtree_index:select({0, 0, 0, 0},
           {iterator = 'neighbor', fetch_pos=false,
            limit=1, after=pos})

        t.assert_equals(tuples, {{3541,{4,1,5,3,2,5,-10,4}}})
    end)
end

g.test_rtree_pagination_equal_distance = function()
    g.server:exec(function()

        local s = box.schema.space.create('equal_distance')
        s:format({ { type = 'number', name = 'id' },
            { type = 'array', name = 'content' } })
        local hash_index = s:create_index('primary',
            { type = 'tree', parts = {'id'} })
        local rtree_index = s:create_index('rtree_ind',
            { type = 'rtree', unique = false,
             parts = {'content'} })

        local tuples = {{21,{50,0}}, {20,{43.301,25}},
           {17,{25,43.301}}, {16,{0,50}}, {4,{-25,43.301}},
           {7,{-43.301,25}}, {13,{-50, 0}}, {0,{-43.301,-25}},
           {25,{-25,-43.301}}, {41,{0,-50}}, {40,{25,-43.301}},
           {39,{43.301,-25}}, {1,{35.355,35.355}},
           {6,{11.5,48.659}}, {5,{48.659,-11.5}},
           {2, {-35.355,35.355}}, {3, {35.355,-35.355}}}
        for i = 1, #tuples do
            local first_value, second_tuple = unpack(tuples[i])
            s:insert({first_value, second_tuple})
        end

        local pos
        hash_index:select{0}

        tuples, pos = rtree_index:select({0, 0},
           {iterator = 'neighbor',
           fetch_pos=true, limit=5})

        t.assert_equals(tuples,
           {{5,{48.659,-11.5}},{6,{11.5,48.659}},
            {1,{35.355,35.355}},{2,{-35.355,35.355}},
            {3,{35.355,-35.355}}})

        tuples, pos = rtree_index:select({0, 0},
           {iterator = 'neighbor',
           fetch_pos=true, limit=4, after=pos})

        t.assert_equals(tuples,
           {{0,{-43.301,-25}},{4,{-25,43.301}},
            {7,{-43.301,25}},{17,{25,43.301}}})

        tuples, pos = rtree_index:select({0, 0},
            {iterator = 'neighbor',
            fetch_pos=true, limit=4, after=pos})

        t.assert_equals(tuples,
            {{20,{43.301,25}},{25,{-25,-43.301}},
             {39,{43.301,-25}},{40,{25,-43.301}}})

        tuples, pos = rtree_index:select({0, 0},
            {iterator = 'neighbor',
            fetch_pos=false, limit=5, after=pos})
        t.assert_equals(tuples,
            {{13,{-50,0}},{16,{0,50}},
             {21,{50,0}},{41,{0,-50}}})

        t.assert_equals(pos, nil)
    end)
end

g.test_rtree_pagination_manhattan = function()
    g.server:exec(function()

        local s = box.schema.space.create(
            'manhattan')
        s:format({ { type = 'number', name = 'id' },
            { type = 'array', name = 'content' } })
        local hash_index = s:create_index('primary',
            { type = 'tree', parts = {'id'} })
        local rtree_index = s:create_index('rtree_ind',
            { type = 'rtree', unique = false,
            distance = 'manhattan',
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
            {{30,{2,13,-2,-20}},{50,{4,13,-4,-20}},
             {70,{6,13,-6,-20}},{29,{2,12,-2,-18}}})

        tuples, pos = rtree_index:select({1, 0},
            {iterator = 'neighbor',
            fetch_pos=true, limit=4, after=pos})

        t.assert_equals(tuples,
            {{49,{4,12,-4,-18}},{69,{6,12,-6,-18}},
             {28,{2,11,-2,-16}},{48,{4,11,-4,-16}}})

        tuples, pos = rtree_index:select({1, 0},
            {iterator = 'neighbor',
            fetch_pos=false, limit=3, after=pos})

        t.assert_equals(tuples,
            {{68,{6,11,-6,-16}},{27,{2,10,-2,-14}},
             {47,{4,10,-4,-14}}})

        tuples, pos = rtree_index:select({1, 0},
            {iterator = 'neighbor',
            fetch_pos=true, limit=0, after=pos})

        t.assert_equals(tuples, {})
        t.assert_equals(pos, nil)
    end)
end

g.test_rtree_pagination_delete_tuple = function()
    g.server:exec(function()

        local s = box.schema.space.create('delete_tuple')
        s:format({ { type = 'number', name = 'id' },
            { type = 'array', name = 'content' } })
        local hash_index = s:create_index('primary',
            { type = 'tree', parts = {'id'} })
        local rtree_index = s:create_index('rtree_ind',
            { type = 'rtree', unique = false,
             distance = 'manhattan',
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

        tuples, pos = rtree_index:select({1, 1},
            {iterator = 'neighbor',
            fetch_pos=true, limit=4})

        t.assert_equals(tuples,
            {{30,{2,13,-2,-20}},{50,{4,13,-4,-20}},
             {70,{6,13,-6,-20}},{29,{2,12,-2,-18}}})

        s:delete{29}

        tuples, pos = rtree_index:select({1, 1},
            {iterator = 'neighbor',
            fetch_pos=true, limit=4, after=pos})

        t.assert_equals(tuples,
            {{49,{4,12,-4,-18}},{69,{6,12,-6,-18}},
             {28,{2,11,-2,-16}},{48,{4,11,-4,-16}}})

        s:delete{48}
        s:delete{68}

        tuples, pos = rtree_index:select({1, 1},
            {iterator = 'neighbor',
            fetch_pos=true, limit=4, after=pos})
        t.assert_equals(tuples,
            {{27,{2,10,-2,-14}},{47,{4,10,-4,-14}},
             {67,{6,10,-6,-14}},{26,{2,9,-2,-12}}})

        s:delete{26}

        tuples, pos = rtree_index:select({1, 1},
            {iterator = 'neighbor',
            fetch_pos=false, limit=4, after=pos})
        t.assert_equals(tuples,
            {{46,{4,9,-4,-12}},{66,{6,9,-6,-12}},
             {25,{2,8,-2,-10}},{45,{4,8,-4,-10}}})
        t.assert_equals(pos, nil)
    end)
end

g.test_rtree_pagination_errors = function()
    g.server:exec(function()

        local s = box.schema.space.create('errors')
        s:format({ { type = 'number', name = 'id' },
            { type = 'array', name = 'content' } })
        local hash_index = s:create_index('primary',
            { type = 'tree', parts = {'id'} })
        local rtree_index = s:create_index('rtree_ind',
            { type = 'rtree', unique = false,
             parts = {'content'} })

        hash_index:select{0};

        local iters = {'eq', 'le', 'lt', 'ge', 'gt',
            'overlaps'}

        for _, iter in pairs(iters) do
            t.assert_error_msg_contains(
              'does not support pagination',
              rtree_index.select,
              rtree_index, {0, 0},
              {iterator = iter, fetch_pos=true})
        end
        s:insert{1, {1, 1}}
        local tuples, pos = rtree_index:select({0, 0},
            {iterator = 'neighbor',
            fetch_pos=true, limit=1})
        t.assert_equals(tuples[1], {1, {1, 1}})

        for _, iter in pairs(iters) do
            t.assert_error_msg_contains(
              'pagination with wrong iterator type.',
              rtree_index.select,
              rtree_index, {0, 0},
              {iterator = iter, fetch_pos=true,
               after=pos})
        end
    end)
end
