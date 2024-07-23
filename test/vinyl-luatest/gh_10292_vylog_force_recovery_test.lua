local fio = require('fio')
local server = require('luatest.server')
local t = require('luatest')

local g = t.group()

g.before_each(function(cg)
    t.tarantool.skip_if_not_debug()
    cg.server = server:new()
    cg.server:start()
end)

g.after_each(function(cg)
    cg.server:drop()
end)

g.test_force_recovery = function(cg)
    cg.server:exec(function()
        local s = box.schema.space.create('test', {engine = 'vinyl'})
        s:create_index('pk')

        -- Create a vylog file with bad records.
        box.error.injection.set('ERRINJ_VY_LOG_WRITE_BAD_RECORDS', true)
        box.snapshot()
        box.error.injection.set('ERRINJ_VY_LOG_WRITE_BAD_RECORDS', false)

        -- Checkpoint should fail because of the bad vylog file.
        s:insert({1})
        t.assert_error_covers({
            type = 'ClientError',
            code = box.error.INVALID_VYLOG_FILE,
        }, box.snapshot)
    end)

    -- The server should fail to restart because of the bad vylog file.
    cg.server:restart(nil, {wait_until_ready = false})
    t.assert(cg.server:grep_log('INVALID_VYLOG_FILE', nil, {
        filename = fio.pathjoin(cg.server.workdir, cg.server.alias .. '.log'),
    }))

    -- force_recovery should help.
    cg.server:restart({box_cfg = {force_recovery = true}})
    local patterns = {
        'failed to process vylog record: drop_lsm{lsm_id=100501, }',
        'INVALID_VYLOG_FILE.* LSM tree 100501 deleted but not registered',
        'failed to decode vylog record: %[15, {0: 100502, 6: 12345}%]',
        'INVALID_VYLOG_FILE.* Bad record: missing key definition',
        'INVALID_VYLOG_FILE.* LSM tree 12345/0 prepared twice',
        'skipping invalid vylog record',
    }
    for _, pattern in ipairs(patterns) do
        t.assert(cg.server:grep_log(pattern), pattern)
    end
    cg.server:exec(function()
        t.assert_equals(box.space.test:select(), {{1}})
        box.snapshot()
    end)

    -- force_recovery isn't required after a checkpoint is created.
    cg.server:restart({box_cfg = {force_recovery = false}})
    cg.server:exec(function()
        t.assert_equals(box.space.test:select(), {{1}})
        box.space.test:drop()
        box.snapshot()
    end)
end
