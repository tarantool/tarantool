local t = require('luatest')
local server = require('luatest.server')

local g = t.group()

g.before_all(function(g)
    t.tarantool.skip_if_not_debug()
    -- Let's follow proposed 10142 reproducer as close as possible
    -- Step 1: Start a Tarantool built in debug mode
    g.server = server:new({alias = 'master'})
    g.server:start()
    g.server:exec(function()
        -- Step 2: Create a space
        local s = box.schema.space.create('test')
        s:format({{ name = 'pk', type = 'unsigned' },})
        s:create_index('primary', { parts = { 'pk' } })
    end)
end)

g.after_all(function(g)
    g.server:drop()
end)

g.before_each(function(g)
    g.server:exec(function() box.space.test:truncate() end)
end)

local function test_template(server, use_wal_sync)
    return server:exec(function(use_wal_sync)
        local fiber = require('fiber')
        -- Step 3.1: Insert wal delay so it's possible to commit
        -- but not writing to the wal affecting lsn
        box.error.injection.set('ERRINJ_WAL_DELAY', true)
        -- Step 3.2: Save initial lsn value
        local lsn_before = box.info.lsn

        local lsn_save = lsn_before
        local lsn_save_finished = false
        local lsn_insert_finished = false
        fiber.new(function()
            -- Step 4: Initiate long write which we need to
            -- wait for some reason
            box.space.test:insert{1}
            lsn_insert_finished = true
        end)
        fiber.new(function()
            -- Step 5
            fiber.yield() -- why not yield here?
            if use_wal_sync then
                box.ctl.wal_sync()
            end
            -- Step 6: Save the result lsn
            lsn_save = box.info.lsn

            lsn_save_finished = true
        end)
        -- This sleep is added here intentionally to increase the
        -- probability of lsn_save fiber being finished before the
        -- insert so only proper wal_sync may help fibers to be ordered
        -- correctly
        fiber.sleep(0.3)
        -- Step 7: re-enable wal again so write can happen
        -- now
        box.error.injection.set('ERRINJ_WAL_DELAY', false)
        t.helpers.retrying({delay = 0.1}, function()
            t.assert_equals(lsn_insert_finished, true)
        end)
        -- Step 8: lsn is guaranteed to be after that transaction
        local lsn_after = box.info.lsn
        t.helpers.retrying({delay = 0.1}, function()
            t.assert_equals(lsn_save_finished, true)
        end)
        return lsn_before, lsn_save, lsn_after
    end, {use_wal_sync})
end

g.test_box_ctl_no_val_sync = function(g)
    local lsn_before, lsn_save, lsn_after = test_template(g.server, false)
    -- wal_sync() doesn't happen
    t.assert_equals(lsn_after, lsn_save + 1)
    -- insert{1} happens
    t.assert_equals(lsn_save, lsn_before)
end

g.test_box_ctl_val_sync = function(g)
    local lsn_before, lsn_save, lsn_after = test_template(g.server, true)
    -- wal_sync() happenes
    t.assert_equals(lsn_after, lsn_save)
    -- insert{1} happens
    t.assert_equals(lsn_after, lsn_before + 1)
end
