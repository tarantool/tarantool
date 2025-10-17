local server = require('luatest.server')
local t = require('luatest')

local g = t.group('gh-11929-mvcc-failing-index-count-limit-assertion')
--
-- gh-11929: mvcc failing index count limit assertion
--

g.before_all(function()
    t.tarantool.skip_if_not_debug()

    g.server = server:new{box_cfg = {memtx_use_mvcc_engine = true}}
    g.server:start()

    g.server:exec(function()
        box.schema.space.create("test")
    end)
end)

g.after_each(function()
    g.server:exec(function() box.space.test:truncate() end)
end)

g.after_all(function()
    g.server:drop()
end)

g.test_index_id = function()
    g.server:exec(function()
        local function space_format(num_fields)
            local space_format = {}
            for i = 1, num_fields do
                table.insert(space_format, {
                    name = ('field_%d'):format(i),
                    type = 'unsigned',
                })
            end
            return space_format
        end

        local function index_opts(format, field_id)
            return {parts={{format[field_id].name}}, unique=true}
        end

        local index_count = 128

        local format = space_format(index_count)
        box.space.test:format(format)

        for i = 1, index_count do
            box.space.test:create_index(
                ('idx_%d'):format(i),
                index_opts(format, i)
            )
        end

        local fun = require('fun')
        local tuple = fun.totable(fun.take(index_count, fun.duplicate(1)))

        box.space.test:replace(tuple)
    end)
end
