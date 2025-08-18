local server = require('luatest.server')
local t = require('luatest')

local g = t.group('rtree-neighbor-sort')

g.before_all(function()
    g.server = server:new()
    g.server:start()
end)

g.after_all(function()
    g.server:stop()
end)

g.test_rtree_equal_distance = function()
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

        hash_index:select{0}

        tuples = rtree_index:select({0, 0},
           {iterator = 'neighbor', limit=17})

        t.assert_equals(tuples,
           {{5,{48.659,-11.5}},{6,{11.5,48.659}},
            {1,{35.355,35.355}},{2,{-35.355,35.355}},
            {3,{35.355,-35.355}},{0,{-43.301,-25}},
            {4,{-25,43.301}},{7,{-43.301,25}},
            {17,{25,43.301}},{20,{43.301,25}},
            {25,{-25,-43.301}},{39,{43.301,-25}},
            {40,{25,-43.301}},{13,{-50,0}},
            {16,{0,50}},{21,{50,0}},{41,{0,-50}}})
    end)
end