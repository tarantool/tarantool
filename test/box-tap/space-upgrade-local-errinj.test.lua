#!/usr/bin/env tarantool

local tap = require('tap')
local test = tap.test('space-upgrade-local-errinj')

test:plan(23)

local fiber = require('fiber')

local functions = {
    -- name, body
    {
        name   = "upgrade_func",
        body   = [[
                  function(tuple)
                      local new_tuple = {}
                      new_tuple[1] = tuple[1]
                      new_tuple[2] = tostring(tuple[2]).."upgraded"
                      return new_tuple
                  end
                 ]],
    }
}

local function cleanup_functions()
    for _, v in pairs(functions) do
        box.schema.func.drop(v['name'])
    end
end

local function check_space_upgrade_empty()
    local cnt = box.space._space_upgrade.index[0]:count()
    test:is_deeply(cnt, 0, "No entries after running upgrade")
    test:is(box.space.t.upgrade_state, nil, "No upgrade is in progress")
end

local function check_space_is_not_locked()
    local new_format  = {{name="f1", type="unsigned"},
                         {name="f2", type="string"} }
    local err_msg = 'Tuple field 2 (f2) type does not match one required by operation: expected string, got unsigned'
    -- Lock acquiring is checked BEFORE space format check, so if
    -- box.space.t.format fails due to wrong format, then it wasn't locked.
    --
    local ok,err = pcall(box.space.t.format, box.space.t, new_format)
    test:is(ok, false, "Alter has failed")
    test:is_deeply(err_msg, tostring(err), "Space is not locked")
end

local function finalize()
    cleanup_functions()
    box.space.t:drop()
end

local rows_cnt = 20

local function setup()
    box.cfg{}
    -- Create and fill in casual space to test basic capabilities.
    local t = box.schema.create_space("t")
    t:create_index("pk")
    t:format({{"f1", "unsigned"}, {"f2", "unsigned"}})
    for i = 1, rows_cnt do
        t:replace({i, i})
    end

    for _, v in pairs(functions) do
        box.schema.func.create(v['name'], {body = v['body'], is_deterministic = true, is_sandboxed = true})
    end
end

local new_format  = {{name="f1", type="unsigned"}, {name="f2", type="string"} }
local disk_fail_err_msg = "Failed to write to disk"

local function test_on_create_rollback_trigger()
    box.error.injection.set('ERRINJ_WAL_WRITE_DISK', true)
    local ok,err = pcall(box.space.t.upgrade, box.space.t, {mode = "test", func = "upgrade_func", format = new_format})
    test:is(ok, false, "Upgrade has failed")
    test:is_deeply(disk_fail_err_msg, tostring(err), "Proper error message is raised")
    check_space_upgrade_empty()
    check_space_is_not_locked()

    -- The same but in "notest" mode.
    ok,err = pcall(box.space.t.upgrade, box.space.t, {mode = "notest", func = "upgrade_func", format = new_format})
    test:is(ok, false, "Upgrade has failed")
    test:is_deeply(disk_fail_err_msg, tostring(err), "Proper error message is raised")
    check_space_upgrade_empty()
    check_space_is_not_locked()

    box.error.injection.set('ERRINJ_WAL_DELAY', true)

    -- Alter format right after insertion in _space_upgrade.
    local f1 = fiber.create(box.space._space_upgrade.replace, box.space._space_upgrade,
        {box.space.t.id, "inprogress", box.func.upgrade_func.id, new_format, box.info.uuid})
    f1:set_joinable(true)
    local _ = fiber.create(box.space.t.format, box.space.t, new_format)
    test:is_deeply(box.space.t:format(), new_format, "Format has been changed")
    box.error.injection.set('ERRINJ_WAL_DELAY', false)
    f1:join()
    check_space_upgrade_empty()
    check_space_is_not_locked()

    box.error.injection.set('ERRINJ_WAL_WRITE_DISK', false)
end

local function test_on_delete_rollback_trigger()
    local ok, _ = pcall(box.space._space_upgrade.insert, box.space._space_upgrade,
        {box.space.t.id, "test", box.func.upgrade_func.id, new_format, box.info.uuid})
    test:is(ok, true, "Successful insertion to _space_upgrade")
    box.error.injection.set('ERRINJ_WAL_DELAY', true)
    box.error.injection.set('ERRINJ_WAL_WRITE_DISK', true)
    local f1 = fiber.create(box.space._space_upgrade.delete, box.space._space_upgrade, {box.space.t.id})
    f1:set_joinable(true)
    test:is(box.space.t.upgrade_state, nil, "Upgrade has been deleted")
    box.error.injection.set('ERRINJ_WAL_DELAY', false)
    f1:join()
    test:is(tostring(box.space.t.upgrade_state.status), "test", "Upgrade has been reverted")
    box.error.injection.set('ERRINJ_WAL_WRITE_DISK', false)
    box.space._space_upgrade:delete({box.space.t.id})
end

local function test_on_replace_rollback_trigger()
    local ok, _ = pcall(box.space._space_upgrade.insert, box.space._space_upgrade,
        {box.space.t.id, "test", box.func.upgrade_func.id, new_format, box.info.uuid})
    test:is(ok, true, "Successful insertion to _space_upgrade")
    box.error.injection.set('ERRINJ_WAL_DELAY', true)
    box.error.injection.set('ERRINJ_WAL_WRITE_DISK', true)
    local f1 = fiber.create(box.space._space_upgrade.replace, box.space._space_upgrade,
        {box.space.t.id, "inprogress", box.func.upgrade_func.id, new_format, box.info.uuid})
    f1:set_joinable(true)
    test:is(box.space.t.upgrade_state.status, "inprogress", "Upgrade status has been changed")
    box.error.injection.set('ERRINJ_WAL_DELAY', false)
    f1:join()
    test:is(tostring(box.space.t.upgrade_state.status), "test", "Upgrade has been reverted")
    box.error.injection.set('ERRINJ_WAL_WRITE_DISK', false)
    box.space._space_upgrade:delete({box.space.t.id})
end

local function test_upgrade_wal_fail()
    test_on_create_rollback_trigger()
    test_on_delete_rollback_trigger()
    test_on_replace_rollback_trigger()
end

local function run_all()
    setup()
    test_upgrade_wal_fail()
    finalize()
end

run_all()

os.exit(test:check() and 0 or 1)
