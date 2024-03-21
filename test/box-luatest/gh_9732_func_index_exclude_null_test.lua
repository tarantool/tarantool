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
        if box.space.space ~= nil then
            box.space.space:drop()
        end
        if box.func.func ~= nil then
            box.func.func:drop()
        end
        if box.func.multipart_func ~= nil then
            box.func.multipart_func:drop()
        end
    end)
end)

g.test_func_index_exclude_null = function(cg)
    cg.server:exec(function()
        local space = box.schema.space.create("space")
        space:format({
            {name = "first", type = "unsigned"},
            {name = "second", type = "unsigned", is_nullable = true},
            {name = "third", type = "unsigned", is_nullable = true}
        })
        space:create_index("primary",
            {parts = {{field = 1, type = "unsigned"}}})

        box.schema.func.create("func", {
            body = [[
                function(tuple)
                    return {tuple[2]}
                end
            ]],
            is_deterministic = true,
            is_sandboxed = true,
        })
        space:create_index("index", {
            unique = false,
            func = "func",
            parts = {{field = 1, type = "unsigned",
                      is_nullable = true, exclude_null = true}},
        })

        box.schema.func.create("multipart_func", {
            body = [[
                function(tuple)
                    return {tuple[2], tuple[3]}
                end
            ]],
            is_deterministic = true,
            is_sandboxed = true,
        })
        space:create_index("multipart_index", {
            unique = false,
            func = "multipart_func",
            parts = {
                {field = 1, type = "unsigned",
                 is_nullable = true, exclude_null = true},
                {field = 2, type = "unsigned",
                 is_nullable = true, exclude_null = true}
            }
        })

        space:insert({1, 1, 1})
        space:insert({2, 2, box.NULL})
        space:insert({3, box.NULL, 3})
        space:insert({4, box.NULL, box.NULL})
    end)

    local function check_case()
        local index = box.space.space.index.index
        t.assert_equals(
            index:select({}, {fullscan = true}), {{1, 1, 1}, {2, 2}})
        t.assert_equals(index:select({1}), {{1, 1, 1}})
        t.assert_equals(index:select({2}), {{2, 2}})
        t.assert_equals(index:select({3}), {})
        t.assert_equals(index:select({4}), {})
        t.assert_equals(index:select({box.NULL}), {})

        local multipart = box.space.space.index.multipart_index
        t.assert_equals(multipart:select({}, {fullscan = true}), {{1, 1, 1}})
        t.assert_equals(multipart:select({1}), {{1, 1, 1}})
        t.assert_equals(multipart:select({2}), {})
        t.assert_equals(multipart:select({3}), {})
        t.assert_equals(multipart:select({4}), {})
        t.assert_equals(multipart:select({box.NULL}), {})
    end

    cg.server:exec(check_case)

    -- Check after recovery
    cg.server:restart()
    cg.server:exec(check_case)
end

g.test_func_index_exclude_null_multikey = function(cg)
    cg.server:exec(function()
        local space = box.schema.space.create("space")
        space:format({
            {name = "first", type = "unsigned"},
            {name = "second", type = "unsigned"}
        })
        space:create_index("primary",
            {parts = {{field = 1, type = "unsigned"}}})

        box.schema.func.create("func", {
            body = [[
                function(tuple)
                    local box_NULL = tuple[3]
                    assert(type(box_NULL) == 'cdata')
                    assert(box_NULL == nil)

                    local last_val = box_NULL
                    if tuple[1] == 0 then
                        last_val = 'abc'
                    end
                    return {
                        {box_NULL}, {tuple[1]}, {box_NULL},
                        {tuple[2]}, {last_val}
                    }
                end
            ]],
            is_deterministic = true,
            is_sandboxed = true,
            opts = {is_multikey = true},
        })
        space:create_index("index", {
            unique = false,
            func = "func",
            parts = {{field = 1, type = "unsigned",
                      is_nullable = true, exclude_null = true}}
        })

        box.schema.func.create("multipart_func", {
            body = [[
                function(tuple)
                    local box_NULL = tuple[3]
                    assert(type(box_NULL) == 'cdata')
                    assert(box_NULL == nil)
                    return {
                        {tuple[1], tuple[2]}, {tuple[1], box_NULL},
                        {box_NULL, tuple[2]}, {box_NULL, box_NULL}
                    }
                end
            ]],
            is_deterministic = true,
            is_sandboxed = true,
            opts = {is_multikey = true},
        })
        space:create_index("multipart_index", {
            unique = false,
            func = "multipart_func",
            parts = {
                {field = 1, type = "unsigned", is_nullable = true,
                 exclude_null = true},
                {field = 2, type = "unsigned", is_nullable = true,
                 exclude_null = true}
            }
        })

        -- Insert tuple with invalid last key to check if rollback of inserted
        -- keys of functional multikey works correctly
        local ok = pcall(function() space:insert({0, 0, box.NULL}) end)
        t.assert(not ok)

        space:insert({1, 2, box.NULL})
        space:insert({3, 4, box.NULL})
    end)

    local function check_case()
        local index = box.space.space.index.index
        t.assert_equals(
            index:select({}, {fullscan = true}),
            {{1, 2}, {1, 2}, {3, 4}, {3, 4}}
        )
        t.assert_equals(index:select({1}), {{1, 2}})
        t.assert_equals(index:select({2}), {{1, 2}})
        t.assert_equals(index:select({3}), {{3, 4}})
        t.assert_equals(index:select({4}), {{3, 4}})
        t.assert_equals(index:select({box.NULL}), {})

        local multipart = box.space.space.index.multipart_index
        t.assert_equals(
            multipart:select({}, {fullscan = true}), {{1, 2}, {3, 4}})
        t.assert_equals(multipart:select({1}), {{1, 2}})
        t.assert_equals(multipart:select({1, 2}), {{1, 2}})
        t.assert_equals(multipart:select({3}), {{3, 4}})
        t.assert_equals(multipart:select({3, 4}), {{3, 4}})
        t.assert_equals(multipart:select({box.NULL}), {})
    end

    cg.server:exec(check_case)

    -- Check after recovery
    cg.server:restart()
    cg.server:exec(check_case)
end
