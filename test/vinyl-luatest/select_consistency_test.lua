local server = require('luatest.server')
local t = require('luatest')

local g = t.group('vinyl.select_consistency', {
    {defer_deletes = true}, {defer_deletes = false}
})

g.before_all(function(cg)
    cg.server = server:new({
        alias = 'master',
        box_cfg = {
            log_level = 'verbose',
            vinyl_memory = 1024 * 1024,
            vinyl_cache = 512 * 1024,
            vinyl_run_count_per_level = 1,
            vinyl_run_size_ratio = 4,
            vinyl_page_size = 1024,
            vinyl_range_size = 64 * 1024,
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
            local json = require('json')
            local log = require('log')

            math.randomseed(os.time())
            box.stat.reset()

            local YIELD_PROBABILITY = 0.2
            local WRITE_FIBERS = 20
            local READ_FIBERS = 5
            local MAX_TX_STMTS = 10
            local MAX_KEY = 1000
            local MAX_VAL = 10
            local MAX_MULTIKEY_COUNT = 10
            local MAX_PADDING_SIZE = 1000
            local TEST_TIME = 5

            local s = box.space.test

            local function make_tuple()
                local multikey = {}
                for _ = 1, math.random(MAX_MULTIKEY_COUNT) do
                    table.insert(multikey, math.random(MAX_VAL))
                end
                return {
                    math.random(MAX_KEY), math.random(MAX_VAL),
                    math.random(MAX_VAL), math.random(MAX_VAL),
                    multikey, digest.urandom(math.random(MAX_PADDING_SIZE)),
                }
            end

            local function str_tuple(tuple)
                -- Remove padding (6th field).
                return json.encode({unpack(tuple, 1, 5)})
            end

            local function pick_txn_isolation()
                local variants = {
                    'best-effort',
                    'read-committed',
                    'read-confirmed',
                }
                return variants[math.random(#variants)]
            end

            local function inject_yield()
                if math.random(100) / 100 < YIELD_PROBABILITY then
                    fiber.yield()
                end
            end

            local function do_write()
                local ok, err = pcall(function()
                    local txn_isolation = pick_txn_isolation()
                    log.info('begin %s', txn_isolation)
                    box.begin{txn_isolation = txn_isolation}
                    inject_yield()
                    for _ = 1, math.random(MAX_TX_STMTS) do
                        local op = math.random(4)
                        if op == 1 then
                            local tuple = make_tuple()
                            log.info('replace %s', str_tuple(tuple))
                            s:replace(tuple)
                        elseif op == 2 then
                            local key = math.random(MAX_KEY)
                            log.info('delete %s', key)
                            s:delete(key)
                        elseif op == 3 then
                            local tuple = make_tuple()
                            log.info('update %s', str_tuple(tuple))
                            s:update(tuple[1], {
                                {'=', 2, tuple[2]},
                                {'=', 3, tuple[3]},
                                {'=', 4, tuple[4]},
                                {'=', 5, tuple[5]},
                            })
                        else
                            local tuple = make_tuple()
                            log.info('upsert %s', str_tuple(tuple))
                            s:upsert(tuple, {
                                {'=', 2, tuple[2]},
                                {'=', 3, tuple[3]},
                                {'=', 4, tuple[4]},
                                {'=', 5, tuple[5]},
                            })
                        end
                        inject_yield()
                    end
                    log.info('commit')
                    box.commit()
                    log.info('confirm')
                end)
                if not ok then
                    box.rollback()
                    if err.type == 'ClientError' and
                            err.code == box.error.TRANSACTION_CONFLICT then
                        log.info('rollback with conflict')
                    else
                        log.error('unexpected error: %s', err)
                        error(err)
                    end
                end
            end

            local function check_select(idx1, idx2, iterator)
                local key = math.random(MAX_VAL)
                local opts = {iterator = iterator}
                local res1 = {}
                local res2 = {}
                local ok, err = pcall(function()
                    local txn_isolation = pick_txn_isolation()
                    log.info('begin %s', txn_isolation)
                    box.begin{txn_isolation = txn_isolation}
                    inject_yield()
                    log.info('select %s %s from %s', iterator, key, idx1)
                    for _, tuple in s.index[idx1]:pairs(key, opts) do
                        tuple = tuple:totable()
                        log.info('read %s', str_tuple(tuple))
                        table.insert(res1, tuple)
                        inject_yield()
                    end
                    log.info('select %s %s from %s', iterator, key, idx2)
                    for _, tuple in s.index[idx2]:pairs(key, opts) do
                        tuple = tuple:totable()
                        log.info('read %s', str_tuple(tuple))
                        table.insert(res2, tuple)
                        inject_yield()
                    end
                    log.info('commit')
                    box.commit()
                end)
                if not ok then
                    box.rollback()
                    log.error('unexpected error: %s', err)
                    error(err)
                end
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
                        err = string.format('consistency check failed: ' ..
                                            'key %s from %s not found in %s',
                                            t1[1], idx1, idx2)
                        break
                    end
                end
                if not ok then
                    log.error(err)
                    error(err)
                end
            end

            local function do_read()
                local variants = {
                    {'i2', 'i3', 'eq'},
                    {'i2', 'i3', 'req'},
                    {'i4', 'i5', 'eq'},
                    {'i4', 'i5', 'req'},
                }
                return check_select(unpack(variants[math.random(#variants)]))
            end

            local stop = false

            local function read_fiber_f()
                while not stop do
                    do_read()
                    fiber.yield()
                end
            end

            local function write_fiber_f()
                while not stop do
                    do_write()
                    fiber.yield()
                end
            end

            local fibers = {}
            for _ = 1, READ_FIBERS do
                local f = fiber.new(read_fiber_f)
                f:set_joinable(true)
                table.insert(fibers, f)
            end
            for _ = 1, WRITE_FIBERS do
                local f = fiber.new(write_fiber_f)
                f:set_joinable(true)
                table.insert(fibers, f)
            end
            fiber.sleep(TEST_TIME)
            stop = true
            for _, f in ipairs(fibers) do
                t.assert_equals({f:join(5)}, {true})
            end
        end)
    end
    run_test()

    -- Delete all tuples stored in the primary index, perform major compaction,
    -- and check that there's no garbage statements left.
    cg.server:exec(function()
        local function wait_compaction()
            t.helpers.retrying({timeout = 60}, function()
                t.assert_covers(box.stat.vinyl(), {
                    scheduler = {compaction_queue = 0, tasks_inprogress = 0},
                })
            end)
        end

        -- Wait for all in-progress dump and compaction tasks to complete.
        box.snapshot()
        wait_compaction()

        -- Delete all tuples stored in the primary index.
        local s = box.space.test
        for _, tuple in ipairs(s:select()) do
            s:delete(tuple[1])
        end

        -- Compact the primary index first to generate deferred DELETEs.
        box.snapshot()
        s.index.i1:compact()
        wait_compaction()

        -- Compact the secondary indexes.
        box.snapshot()
        for i = 2, 5 do
            s.index['i' .. i]:compact()
        end
        wait_compaction()

        -- Check that there's no garbage statements left.
        local actual = {}
        local expected = {}
        for i = 1, 5 do
            local k = 'i' .. i
            actual[k] = s.index[k]:stat()
            expected[k] = {rows = 0}
        end
        t.assert_covers(actual, expected)
    end)
end
