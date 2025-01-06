local t = require('luatest')
local treegen = require('luatest.treegen')
local justrun = require('luatest.justrun')
local server = require('luatest.server')
local fiber = require('fiber')

local g = t.group()

g.after_each(function(cg)
    if cg.master then
        cg.master:drop()
    end
end)

g.test_non_empty_limbo_shutdown_wait_mode = function()
    t.tarantool.skip_if_not_debug()
    local script = [[
        box.cfg{}
        local s = box.schema.space.create('test', {is_sync = true})
        s:create_index('pk')
        box.ctl.promote()
        box.cfg{replication_synchro_quorum = 2}
        box.error.injection.set('ERRINJ_WAL_DELAY_DURATION', 0.2);

        box.begin()
        -- Make a dummy trigger to see if it leaks or properly destroyed in
        -- ASAN leaks sanitizer build.
        box.on_commit(function() assert(true) end)
        s:replace{1}
        box.commit({wait = 'none'})

        -- Give the transaction time to reach WAL thread.
        require('fiber').sleep(0.1)
        os.exit(0)
    ]]
    local dir = treegen.prepare_directory({}, {})
    local script_name = 'gh_10766_1.lua'
    treegen.write_file(dir, script_name, script)
    local opts = {nojson = true}
    local res = justrun.tarantool(dir, {}, {script_name}, opts)
    t.assert_equals(res.exit_code, 0)
    t.assert_equals(res.stdout, '')
end

g.test_non_empty_limbo_shutdown_applier = function(cg)
    t.tarantool.skip_if_not_debug()
    cg.master = server:new()
    cg.master:start()
    cg.master:exec(function()
        local s = box.schema.space.create('test', {is_sync = true})
        s:create_index('pk')
        box.ctl.promote()
    end)
    --
    -- Replica waits for a synchronous txn and shuts itself down as soon as the
    -- txn reaches the WAL thread.
    --
    local replica_f = fiber.new(function()
        local script = ([[
            box.cfg{
                bootstrap_strategy = 'legacy',
                replication = "%s",
            }
        ]]):format(cg.master.net_box_uri)
        script = script .. [[
            local fiber = require('fiber')
            local cond = fiber.cond()
            box.space._space:on_replace(function()
                box.on_commit(function()
                    cond:broadcast()
                end)
            end)
            while box.space.test == nil do
                cond:wait()
            end
            box.space.test:on_replace(function()
                -- Make a dummy trigger to see if it leaks or properly destroyed
                -- in ASAN leaks sanitizer build.
                box.on_commit(function() assert(true) end)
            end)
            box.error.injection.set('ERRINJ_WAL_DELAY_DURATION', 0.2);
            while box.info.synchro.queue.len == 0 do
                fiber.sleep(0.001)
            end
            -- Give the transaction time to reach WAL.
            require('fiber').sleep(0.1)
            os.exit(0)
        ]]
        local dir = treegen.prepare_directory({}, {})
        local script_name = 'gh_10766_2.lua'
        treegen.write_file(dir, script_name, script)
        local opts = {nojson = true}
        return justrun.tarantool(dir, {}, {script_name}, opts)
    end)
    replica_f:set_joinable(true)
    cg.master:exec(function()
        -- Only make the txn after the replica is registered and the quorum is
        -- bumped. Otherwise the txn would be committed before the replica joins
        -- and it wouldn't go to the replica's limbo.
        t.helpers.retrying({timeout = 60}, function()
            t.assert_equals(box.info.synchro.quorum, 2)
        end)
        local s = box.space.test
        require('fiber').create(s.replace, s, {1})
    end)
    local ok, res = replica_f:join()
    t.assert(ok)
    t.assert_equals(res.exit_code, 0)
    t.assert_equals(res.stdout, '')
end
