local server = require('luatest.server')
local t = require('luatest')
local g = t.group()

g.before_each(function(cg)
    cg.server = server:new({box_cfg = {memtx_use_mvcc_engine = true}})
    cg.server:start()
    cg.server:exec(function()
        local function table_values_are_zeros(table)
            for _, v in pairs(table) do
                if type(v) ~= 'table' then
                    if v ~= 0 then
                        return false
                    end
                else
                    if not table_values_are_zeros(v) then
                        return false
                    end
                end
            end
            return true
        end

        -- Checks if there are some tuple stories in memtx MVCC.
        local function mvcc_check_has_tuple_stories()
            local tuple_stories = box.stat.memtx.tx().mvcc.tuples
            t.assert(not table_values_are_zeros(tuple_stories))
        end
        rawset(_G, 'mvcc_check_has_tuple_stories', mvcc_check_has_tuple_stories)

        -- Clear all MVCC stories and check if they were really deleted.
        local function mvcc_clear_stories()
            -- A lot of steps to surely delete all stories.
            -- Each no-op step (when there are no stories) is cheap anyway.
            box.internal.memtx_tx_gc(100)
            local tuple_stories = box.stat.memtx.tx().mvcc.tuples
            t.assert(table_values_are_zeros(tuple_stories))
        end
        rawset(_G, 'mvcc_clear_stories', mvcc_clear_stories)
    end)
end)

g.after_each(function(cg)
    cg.server:drop()
end)

-- The case covers a crash when MVCC called a Lua function of functional
-- index on shutdown after Tarantool Lua state was released.
g.test_crash_on_shutdown = function(cg)
    cg.server:exec(function()
        local mvcc_check_has_tuple_stories =
            rawget(_G, 'mvcc_check_has_tuple_stories')

        box.schema.func.create('test', {
            is_deterministic = true,
            body = [[function(tuple)
                return {tuple[1]}
            end]]
        })

        local s = box.schema.space.create('test')
        s:create_index('pk')
        s:replace{1}

        -- Create functional index and delete the tuple
        s:create_index('func', {
            func = 'test',
            parts = {{1, 'unsigned'}},
        })
        s:delete{1}
        -- Check if there are some tuple stories right before shutdown.
        mvcc_check_has_tuple_stories()
    end)
end
