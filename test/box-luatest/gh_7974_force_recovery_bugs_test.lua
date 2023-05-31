local fio = require('fio')
local server = require('luatest.server')
local t = require('luatest')

local g = t.group()

g.before_all(function()
    t.tarantool.skip_if_not_debug()
end)

--
-- Checks that force recovery works with snapshot containing no user spaces
-- and an unknown request type.
--
g.before_test('test_unknown_request_type', function(cg)
    cg.server = server:new({alias = 'gh_7974_1'})
    cg.server:start()
    cg.server:exec(function()
        box.space._schema:insert{'foo', 'bar'} -- bump LSN
        box.error.injection.set('ERRINJ_SNAP_WRITE_UNKNOWN_ROW_TYPE', true)
        box.snapshot()
    end)
    cg.server:stop()
end)

g.test_unknown_request_type = function(cg)
    local s = cg.server
    s:start{wait_until_ready = false}
    local log = fio.pathjoin(s.workdir, s.alias .. '.log')
    t.helpers.retrying({}, function()
        t.assert_not_equals(s:grep_log("can't initialize storage: " ..
                                       "Unknown request type 777", nil,
                                       {filename = log}), nil)
        t.assert_not(s.process:is_alive())
    end)
    s:stop()
    s.box_cfg = {force_recovery = true}
    t.assert(pcall(s.start, s))
end

g.after_test('test_unknown_request_type', function(cg)
    cg.server:drop()
    cg.server = nil
end)

--
-- Checks that force recovery works with snapshot containing no user
-- space requests and an invalid non-insert (raft) request type.
--
g.before_test('test_invalid_non_insert_request', function(cg)
    cg.server = server:new({alias = 'gh_7974_2'})
    cg.server:start()
    cg.server:exec(function()
        box.space._schema:insert{'foo', 'bar'} -- bump LSN
        box.error.injection.set('ERRINJ_SNAP_WRITE_INVALID_SYSTEM_ROW', true)
        box.snapshot()
    end)
    cg.server:stop()
end)

g.test_invalid_non_insert_request = function(cg)
    local s = cg.server
    s:start{wait_until_ready = false}
    local log = fio.pathjoin(s.workdir, s.alias .. '.log')
    t.helpers.retrying({}, function()
        t.assert_not_equals(s:grep_log("can't initialize storage: " ..
                                       "Invalid MsgPack %- raft body", nil,
                                       {filename = log}), nil)
        t.assert_not(s.process:is_alive())
    end)
    s:stop()
    s.box_cfg = {force_recovery = true}
    t.assert(pcall(s.start, s))
end

g.after_test('test_invalid_non_insert_request', function(cg)
    cg.server:drop()
    cg.server = nil
end)

--
-- Checks that force recovery works with snapshot containing an invalid user
-- space request.
--
g.before_test('test_invalid_user_space_request', function(cg)
    cg.server = server:new({alias = 'gh_7974_3'})
    cg.server:start()
    cg.server:exec(function()
        box.space._schema:insert{'foo', 'bar'} -- bump LSN
        box.error.injection.set('ERRINJ_SNAP_WRITE_MISSING_SPACE_ROW', true)
        box.snapshot()
    end)
    cg.server:stop()
end)

g.test_invalid_user_space_request = function(cg)
    local s = cg.server
    s:start{wait_until_ready = false}
    local log = fio.pathjoin(s.workdir, s.alias .. '.log')
    t.helpers.retrying({}, function()
        t.assert_not_equals(s:grep_log("can't initialize storage: " ..
                                       "Space '777' does not exist", nil,
                                       {filename = log}), nil)
        t.assert_not(s.process:is_alive())
    end)
    s:stop()
    s.box_cfg = {force_recovery = true}
    t.assert(pcall(s.start, s))
end

g.after_test('test_invalid_user_space_request', function(cg)
    cg.server:drop()
    cg.server = nil
end)

--
-- Checks that force recovery does not work with snapshot containing one
-- corrupted request.
--
g.before_test('test_first_corrupted_request', function(cg)
    cg.server = server:new({alias = 'gh_7974_4'})
    cg.server:start()
    cg.server:exec(function()
        box.space._schema:insert{'foo', 'bar'} -- bump LSN
        box.error.injection.set('ERRINJ_SNAP_WRITE_CORRUPTED_INSERT_ROW', true)
        box.snapshot()
    end)
    cg.server:stop()
end)

g.test_first_corrupted_request = function(cg)
    local s = cg.server
    s.box_cfg = {force_recovery = true}
    s:start({wait_until_ready = false})
    local log = fio.pathjoin(s.workdir, s.alias .. '.log')
    t.helpers.retrying({}, function()
        t.assert_not_equals(s:grep_log("can't initialize storage: " ..
                                       "can't parse row", nil,
                                       {filename = log}), nil)
        t.assert_not(s.process:is_alive())
    end)
end

g.after_test('test_first_corrupted_request', function(cg)
    cg.server:drop()
    cg.server = nil
end)

--
-- Checks that force recovery works with snapshot containing one valid
-- and one invalid (second) user request.
--
g.before_test('test_second_corrupted_request', function(cg)
    cg.server = server:new({alias = 'gh_7974_5'})
    cg.server:start()
    cg.server:exec(function()
        box.schema.space.create('test')
        box.space.test:create_index('primary')
        box.space.test:insert({1})
        box.error.injection.set('ERRINJ_SNAP_WRITE_CORRUPTED_INSERT_ROW', true)
        box.snapshot()
    end)
    cg.server:stop()
end)

g.test_second_corrupted_request = function(cg)
    local s = cg.server
    s:start({wait_until_ready = false})
    local log = fio.pathjoin(s.workdir, s.alias .. '.log')
    t.helpers.retrying({}, function()
        t.assert_not_equals(s:grep_log("can't initialize storage: " ..
                                       "can't parse row", nil,
                                       {filename = log}), nil)
        t.assert_not(s.process:is_alive())
    end)
    s:stop()
    s.box_cfg = {force_recovery = true}
    t.assert(pcall(s.start, s))
end

g.after_test('test_second_corrupted_request', function(cg)
    cg.server:drop()
    cg.server = nil
end)

--
-- Checks that force recovery does not work with empty snapshot.
--
g.before_test('test_empty_snapshot', function(cg)
    cg.server = server:new({alias = 'gh_7974_6'})
    cg.server:start()
    cg.server:exec(function()
        box.space._schema:insert{'foo', 'bar'} -- bump LSN
        box.error.injection.set('ERRINJ_SNAP_SKIP_ALL_ROWS', true)
        box.snapshot()
    end)
    cg.server:stop()
end)

g.test_empty_snapshot = function(cg)
    local s = cg.server
    s.box_cfg = {force_recovery = true}
    s:start({wait_until_ready = false})
    local log = fio.pathjoin(s.workdir, s.alias .. '.log')
    t.helpers.retrying({}, function()
        t.assert_not_equals(s:grep_log("can't initialize storage: " ..
                                       "Snapshot has no system spaces", nil,
                                       {filename = log}), nil)
        t.assert_not(s.process:is_alive())
    end)
end

g.after_test('test_empty_snapshot', function(cg)
    cg.server:drop()
    cg.server = nil
end)

--
-- Checks that force recovery does not work with snapshot containing no system
-- space requests and only one valid user space request.
--
g.before_test('test_only_user_space_request', function(cg)
    cg.server = server:new({alias = 'gh_7974_7'})
    cg.server:start()
    cg.server:exec(function()
        box.schema.space.create('test')
        box.space.test:create_index('primary')
        box.space.test:insert({1})
        box.error.injection.set('ERRINJ_SNAP_SKIP_DDL_ROWS', true)
        box.snapshot()
    end)
    cg.server:stop()
end)

g.test_only_user_space_request = function(cg)
    local s = cg.server
    s.box_cfg = {force_recovery = true}
    s:start({wait_until_ready = false})
    local log = fio.pathjoin(s.workdir, s.alias .. '.log')
    t.helpers.retrying({}, function()
        t.assert_not_equals(s:grep_log("can't initialize storage: " ..
                                       "Snapshot has no system spaces", nil,
                                       {filename = log}), nil)
        t.assert_not(s.process:is_alive())
    end)
end

g.after_test('test_only_user_space_request', function(cg)
    cg.server:drop()
    cg.server = nil
end)
