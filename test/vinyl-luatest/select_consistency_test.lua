local server = require('luatest.server')
local t = require('luatest')

local g = t.group('vinyl.select_consistency', {
    {defer_deletes = true}, {defer_deletes = false}
})

g.before_all(function(cg)
    cg.server = server:new({
        alias = 'master',
        box_cfg = {
            vinyl_memory = 2 * 1024 * 1024,
            vinyl_cache = 2 * 1024 * 1024,
            vinyl_run_count_per_level = 1,
            vinyl_run_size_ratio = 4,
            vinyl_page_size = 1024,
            vinyl_range_size = 128 * 1024,
            vinyl_bloom_fpr = 0.1,
            vinyl_read_threads = 2,
            vinyl_write_threads = 4,
            vinyl_defer_deletes = cg.params.defer_deletes,
        },
    })
    cg.server:start()
end)

g.after_all(function(cg)
    cg.server:drop()
end)

g.before_each(function(cg)
    cg.server:exec(function()
        local s = box.schema.create_space('test', {engine = 'vinyl'})
        s:create_index('i1')
        s:create_index('i2', {
            unique = false,
            parts = {{2, 'unsigned'}, {3, 'unsigned'}},
        })
        s:create_index('i3', {
            unique = false,
            parts = {{2, 'unsigned'}, {4, 'unsigned'}},
        })
        s:create_index('i4', {
            unique = false,
            parts = {{'[5][*]', 'unsigned'}, {3, 'unsigned'}},
        })
        s:create_index('i5', {
            unique = false,
            parts = {{'[5][*]', 'unsigned'}, {4, 'unsigned'}},
        })
    end)
end)

g.after_each(function(cg)
    cg.server:exec(function()
        if box.space.test then
            box.space.test:drop()
        end
    end)
end)

g.test_select_consistency = function(cg)
    -- Checks that SELECT over different indexes called in the same transaction
    -- yield the same results under heavy load.
    local function run_test()
        cg.server:exec(function()
            local digest = require('digest')
            local fiber = require('fiber')

            math.randomseed(os.time())
            box.stat.reset()

            local WRITE_FIBERS = 20
            local READ_FIBERS = 5
            local MAX_TX_STMTS = 10
            local MAX_KEY = 100 * 1000
            local MAX_VAL = 1000
            local MAX_MULTIKEY_COUNT = 10
            local PADDING_SIZE = 100
            local DUMP_COUNT = 10

            local s = box.space.test

            local function make_tuple()
                local multikey = {}
                for _ = 1, math.random(MAX_MULTIKEY_COUNT) do
                    table.insert(multikey, math.random(MAX_VAL))
                end
                return {
                    math.random(MAX_KEY), math.random(MAX_VAL),
                    math.random(MAX_VAL), math.random(MAX_VAL),
                    multikey, digest.urandom(PADDING_SIZE),
                }
            end

            local function do_write()
                box.atomic(function()
                    for _ = 1, math.random(MAX_TX_STMTS) do
                        local op = math.random(3)
                        if op == 1 then
                            s:replace(make_tuple())
                        elseif op == 2 then
                            s:delete({math.random(MAX_KEY)})
                        else
                            local tuple = make_tuple()
                            s:upsert(tuple, {
                                {'=', 2, tuple[2]},
                                {'=', 3, tuple[3]},
                                {'=', 4, tuple[4]},
                                {'=', 5, tuple[5]},
                            })
                        end
                    end
                end)
            end

            local failed = {}
            local function check_select(idx1, idx2, iterator)
                local key = {math.random(MAX_VAL)}
                local opts = {iterator = iterator}
                local res1 = {}
                local res2 = {}
                box.atomic(function()
                    for _, tuple in s.index[idx1]:pairs(key, opts) do
                        table.insert(res1, tuple:totable())
                    end
                    for _, tuple in s.index[idx2]:pairs(key, opts) do
                        table.insert(res2, tuple:totable())
                    end
                end)
                local ok = true
                for _, t1 in ipairs(res1) do
                    local found = false
                    for _, t2 in ipairs(res2) do
                        if t1[1] == t2[1] then
                            found = true
                            break
                        end
                    end
                    if not found then
                        ok = false
                        break
                    end
                end
                if not ok then
                    table.insert(failed, {
                        iterator = iterator,
                        idx1 = idx1,
                        idx2 = idx2,
                        res1 = res1,
                        res2 = res2,
                    })
                    error('consistency check failed')
                end
            end

            local function do_read()
                check_select('i2', 'i3', 'eq')
                check_select('i2', 'i3', 'req')
                check_select('i4', 'i5', 'eq')
                check_select('i4', 'i5', 'req')
            end

            local stop = false

            local read_fiber_ch = fiber.channel(1)
            local function read_fiber_f()
                while not stop do
                    local status, err = pcall(do_read)
                    if not status then
                        read_fiber_ch:put(err)
                        return
                    end
                    fiber.yield()
                end
                read_fiber_ch:put(true)
            end

            local write_fiber_ch = fiber.channel(1)
            local function write_fiber_f()
                while not stop do
                    local status, err = pcall(do_write)
                    if not status and
                       err.code ~= box.error.TRANSACTION_CONFLICT then
                        write_fiber_ch:put(err)
                        return
                    end
                    fiber.yield()
                end
                write_fiber_ch:put(true)
            end

            for _ = 1, READ_FIBERS do
                fiber.create(read_fiber_f)
            end
            for _ = 1, WRITE_FIBERS do
                fiber.create(write_fiber_f)
            end
            t.helpers.retrying({timeout = 60}, function()
                t.assert_ge(box.stat.vinyl().scheduler.dump_count, DUMP_COUNT)
            end)
            stop = true
            for _ = 1, READ_FIBERS do
                t.assert_equals(read_fiber_ch:get(5), true)
            end
            for _ = 1, WRITE_FIBERS do
                t.assert_equals(write_fiber_ch:get(5), true)
            end
            t.assert_equals(failed, {})
        end)
    end
    run_test()
    -- Restart and try again.
    cg.server:restart()
    run_test()
    -- Peform major compaction and check that there is no garbage statements.
    cg.server:exec(function()
        local s = box.space.test
        box.stat.reset()
        box.snapshot()
        s.index.i1:compact()
        t.helpers.retrying({timeout = 60}, function()
            t.assert_equals(box.stat.vinyl().scheduler.compaction_queue, 0)
            t.assert_equals(box.stat.vinyl().scheduler.tasks_inprogress, 0)
        end)
        box.snapshot()
        s.index.i2:compact()
        s.index.i3:compact()
        s.index.i4:compact()
        s.index.i5:compact()
        t.helpers.retrying({timeout = 60}, function()
            t.assert_equals(box.stat.vinyl().scheduler.compaction_queue, 0)
            t.assert_equals(box.stat.vinyl().scheduler.tasks_inprogress, 0)
        end)
        t.assert_equals(s.index.i1:len(), s.index.i1:count())
        t.assert_equals(s.index.i2:len(), s.index.i3:len())
        t.assert_equals(s.index.i4:len(), s.index.i5:len())
    end)
end
