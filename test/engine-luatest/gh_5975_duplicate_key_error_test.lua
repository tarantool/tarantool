local t = require('luatest')

local server = require('luatest.server')

local g = t.group(nil, t.helpers.matrix{engine = {'memtx', 'vinyl'}})

g.before_all(function(cg)
    cg.server = server:new()
    cg.server:start()
end)

g.after_all(function(cg)
    cg.server:drop()
end)

g.after_each(function(cg)
    cg.server:exec(function()
        if box.space.test ~= nil then
            box.space.test:drop()
        end
    end)
end)

g.test_duplicate_key_error = function(cg)
    cg.server:exec(function(engine)
        local function check_err(space_name, index_name, old_tuple, new_tuple)
            local space = box.space[space_name]
            t.assert_error_covers({
                type = 'ClientError',
                code = box.error.TUPLE_FOUND,
                message = string.format(
                    'Duplicate key exists in unique index "%s" in space ' ..
                    '"%s" with old tuple - %s and new tuple - %s',
                    index_name, space_name,
                    box.tuple.new(old_tuple), box.tuple.new(new_tuple)),
                space = space_name,
                index = index_name,
                old_tuple = old_tuple,
                new_tuple = new_tuple,
            }, space.insert, space, new_tuple)
        end

        local s = box.schema.space.create('test', {
            engine = engine,
            format = {
                {name = 'a', type = 'unsigned'},
                {name = 'b', type = 'unsigned'},
            },
        })
        s:create_index('pk', {parts = {'a'}})
        s:create_index('i1', {parts = {'b'}, unique = true})
        s:create_index('i2', {parts = {'b'}, unique = true})
        s:insert({1, 1})

        check_err('test', 'pk', {1, 1}, {1, 2})
        check_err('test', 'i1', {1, 1}, {2, 1})
        t.assert_equals(s:select({}, {fullscan = true}), {{1, 1}})
        s.index.i1:drop()
        check_err('test', 'pk', {1, 1}, {1, 2})
        check_err('test', 'i2', {1, 1}, {2, 1})
        t.assert_equals(s:select({}, {fullscan = true}), {{1, 1}})
        s.index.i2:drop()
        check_err('test', 'pk', {1, 1}, {1, 2})
        s:insert({2, 1})
        t.assert_equals(s:select({}, {fullscan = true}), {{1, 1}, {2, 1}})
    end, {cg.params.engine})
end
