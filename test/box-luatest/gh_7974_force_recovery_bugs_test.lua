local fio = require('fio')
local server = require('luatest.server')
local t = require('luatest')

local g = t.group()

-- Checks that force recovery works with snapshot containing no user spaces
-- and an unknown request type.
--
-- Snapshot generation instruction:
-- 1. Patch this line to encode an unknown request type (e.g., 777):
-- luacheck: no max comment line length
-- https://github.com/tarantool/tarantool/blob/cba4e20aa2600713b55bdaad6fd84396257835f2/src/box/xrow.c#L1218;
-- 2. Build and run Tarantool, call `box.cfg`;
-- 3. Bump LSN, e.g., `box.space._schema:insert{'foo', 'bar'}`;
-- 4. Call `box.snapshot`.
g.test_unknown_request_type_force_recovery = function()
    local datadir = 'test/box-luatest/gh_7974_data/unknown_request_type'
    local s = server:new(
        {
            alias = 'unknown_request_type_ok',
            box_cfg = {force_recovery = true},
            datadir = datadir,
        })
    s:start()
    s:drop()

    s = server:new(
        {
            alias = 'unknown_request_type_fail',
            box_cfg = {force_recovery = false},
            datadir = datadir,
        })
    s:start{wait_until_ready = false}
    local log = fio.pathjoin(s.workdir, s.alias .. '.log')
    t.helpers.retrying({}, function()
        t.assert_not_equals(s:grep_log("can't initialize storage: " ..
                                       "Unknown request type 777", nil,
                                       {filename = log}), nil)
    end)
    s:drop()
end

-- Checks that force recovery works with snapshot containing no user
-- space requests and an invalid non-insert (raft) request type.
--
-- Snapshot generation instruction:
-- 1. Patch this line to make the encoding invalid (e.g., just drop it):
-- luacheck: no max comment line length
-- https://github.com/tarantool/tarantool/blob/cba4e20aa2600713b55bdaad6fd84396257835f2/src/box/xrow.c#L1324;
-- 2. Build and run Tarantool, call `box.cfg`;
-- 3. Bump LSN, e.g., `box.space._schema:insert{'foo', 'bar'}`;
-- 4. Call `box.snapshot`.
g.test_invalid_non_insert_request_force_recovery = function()
    local datadir = 'test/box-luatest/gh_7974_data/invalid_non_insert_request'
    local s = server:new(
            {
                alias = 'invalid_non_insert_request_ok',
                box_cfg = {force_recovery = true},
                datadir = datadir,
            })
    s:start()
    s:drop()

    s = server:new(
        {
            alias = 'invalid_non_insert_request_fail',
            box_cfg = {force_recovery = false},
            datadir = datadir,
        })
    s:start{wait_until_ready = false}
    local log = fio.pathjoin(s.workdir, s.alias .. '.log')
    t.helpers.retrying({}, function()
        t.assert_not_equals(s:grep_log("can't initialize storage: " ..
                                       "Invalid MsgPack %- raft body", nil,
                                       {filename = log}), nil)
    end)
    s:drop()
end

-- Checks that force recovery works with snapshot containing an invalid user
-- space request.
--
-- Snapshot generation instruction:
-- 1. Patch this place to change the user space's ID to an invalid one (e.g.,
--    777):
-- luacheck: no max comment line length
-- https://github.com/tarantool/tarantool/blob/cba4e20aa2600713b55bdaad6fd84396257835f2/src/box/memtx_engine.cc#L856.
-- For instance, add this snippet:
-- `if (space_rv->id == 512) space_rv->id = 777;`;
-- 2. Build and run Tarantool, call `box.cfg`;
-- 3. Create a user space, a primary index for it and insert a tuple into it.
-- 4. Call `box.snapshot`.
g.test_invalid_user_space_request_force_recovery = function()
    local datadir = 'test/box-luatest/gh_7974_data/invalid_user_space_request'
    local s = server:new(
            {
                alias = 'invalid_user_space_request_ok',
                box_cfg = {force_recovery = true},
                datadir = datadir,
            })
    s:start()
    s:drop()

    s = server:new(
        {
            alias = 'invalid_user_space_request_fail',
            box_cfg = {force_recovery = false},
            datadir = datadir,
        })
    s:start{wait_until_ready = false}
    local log = fio.pathjoin(s.workdir, s.alias .. '.log')
    t.helpers.retrying({}, function()
        t.assert_not_equals(s:grep_log("can't initialize storage: " ..
                                       "Space '777' does not exist", nil,
                                       {filename = log}), nil)
    end)
    s:drop()
end

-- Checks that force recovery does not work with snapshot containing one
-- corrupted request.
--
-- Snapshot generation instruction:
-- 1. Patch this place to corrupt the replace request body:
-- luacheck: no max comment line length
-- https://github.com/tarantool/tarantool/blob/263d422af33d9cf100ca648b6ca3e00a0f6452cb/src/box/memtx_engine.cc#L683-L684
-- For instance, add this snippet:
-- `if (space_id == 512) body.m_body = 0x8F;`;
-- 2. Build and run Tarantool, call `box.cfg`;
-- 3. Create a user space, a primary index for it and insert a tuple into it.
-- 4. Call `box.snapshot`.
g.test_first_corrupted_request_force_recovery = function()
    local datadir = 'test/box-luatest/gh_7974_data/first_corrupted_request'
    local s = server:new(
            {
                alias = 'first_corrupted_request_fail',
                box_cfg = {force_recovery = true},
                datadir = datadir,
            })
    s:start({wait_until_ready = false})
    local log = fio.pathjoin(s.workdir, s.alias .. '.log')
    t.helpers.retrying({}, function()
        t.assert_not_equals(s:grep_log("can't initialize storage: " ..
                                       "can't parse row", nil,
                                       {filename = log}), nil)
    end)
    s:drop()
end

-- Checks that force recovery works with snapshot containing one valid
-- and one invalid (second) user request.
--
-- Snapshot generation instruction:
-- 1. Patch this place to corrupt the replace request body:
-- luacheck: no max comment line length
-- https://github.com/tarantool/tarantool/blob/263d422af33d9cf100ca648b6ca3e00a0f6452cb/src/box/memtx_engine.cc#L683-L684
-- For instance, add this snippet:
-- `if (space_id == 513) body.m_body = 0x8F;`;
-- 2. Build and run Tarantool, call `box.cfg`;
-- 3. Create two user spaces, create a primary index and insert a tuple into
--    each of them;
-- 4. Call `box.snapshot`.
g.test_second_corrupted_request_force_recovery = function()
    local datadir = 'test/box-luatest/gh_7974_data/second_corrupted_request'
    local s = server:new(
            {
                alias = 'second_corrupted_request_ok',
                box_cfg = {force_recovery = true},
                datadir = datadir,
            })
    s:start()
    s:drop()

    s = server:new(
        {
            alias = 'second_corrupted_request_fail',
            box_cfg = {force_recovery = false},
            datadir = datadir,
        })
    s:start({wait_until_ready = false})
    local log = fio.pathjoin(s.workdir, s.alias .. '.log')
    t.helpers.retrying({}, function()
        t.assert_not_equals(s:grep_log("can't initialize storage: " ..
                                       "can't parse row", nil,
                                       {filename = log}), nil)
    end)
    s:drop()
end

-- Checks that force recovery does not work with empty snapshot.
--
-- Snapshot generation instruction:
-- 1. Patch this place to skip writing anything to snapshot (i.e., comment out
-- the whole block of code):
-- luacheck: no max comment line length
-- https://github.com/tarantool/tarantool/blob/263d422af33d9cf100ca648b6ca3e00a0f6452cb/src/box/memtx_engine.cc#L845-L880
-- For instance, add this snippet:
-- `if (space_id_is_system(space_rv->id) continue;`;
-- 2. Build and run Tarantool, call `box.cfg`;
-- 3. Create one user spaces, create a primary index and insert a tuple into it;
-- 4. Call `box.snapshot`.
g.test_empty_snapshot_force_recovery = function()
    local datadir = 'test/box-luatest/gh_7974_data/empty_snapshot'
    local s = server:new(
            {
                alias = 'empty_snapshot_fail',
                box_cfg = {force_recovery = true},
                datadir = datadir,
            })
    s:start({wait_until_ready = false})
    local log = fio.pathjoin(s.workdir, s.alias .. '.log')
    t.helpers.retrying({}, function()
        t.assert_not_equals(s:grep_log("can't initialize storage: " ..
                                       "Snapshot has no system spaces", nil,
                                       {filename = log}), nil)
    end)
    s:drop()
end

-- Checks that force recovery does not work with snapshot containing no system
-- space requests and only one valid user space request.
--
-- Snapshot generation instruction:
-- 1. Patch this place to skip writing system space requests:
-- luacheck: no max comment line length
-- https://github.com/tarantool/tarantool/blob/263d422af33d9cf100ca648b6ca3e00a0f6452cb/src/box/memtx_engine.cc#L847
-- For instance, add this snippet:
-- `if (space_id_is_system(space_rv->id) continue;`;
-- 2. Build and run Tarantool, call `box.cfg`;
-- 3. Create one user spaces, create a primary index and insert a tuple into it;
-- 4. Call `box.snapshot`.
g.test_only_user_space_request_force_recovery = function()
    local datadir = 'test/box-luatest/gh_7974_data/only_user_space_request'
    local s = server:new(
            {
                alias = 'only_user_space_request_fail',
                box_cfg = {force_recovery = true},
                datadir = datadir,
            })
    s:start({wait_until_ready = false})
    local log = fio.pathjoin(s.workdir, s.alias .. '.log')
    t.helpers.retrying({}, function()
        t.assert_not_equals(s:grep_log("can't initialize storage: " ..
                                       "Snapshot has no system spaces", nil,
                                       {filename = log}), nil)
    end)
    s:drop()
end
