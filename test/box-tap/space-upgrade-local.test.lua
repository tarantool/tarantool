#!/usr/bin/env tarantool

local tap = require('tap')
local test = tap.test('space-upgrade-local')

test:plan(73)

local bad_functions = {
    -- name, body, expected error message, does block upgrade process: if
    -- upgrade fails BEFORE changing space format it is not required to
    -- restart it (since no upgrade artifacts remain).
    {
        name       = [[not_declared_var]],
        body       = [[function(tuple) foo() end]],
        errmsg     = "Upgrade of space t failed: upgrade function has failed: "..
                     "[string \"return function(tuple) foo() end\"]:1: attempt to call global \'foo\' (a nil value)",
        blocking   = true,
    }, {
        name       = [[missing_argument]],
        body       = [[function() return 1 end]],
        errmsg     = [[Upgrade of space t failed: type of return value is expected to be array]],
        blocking   = true,
    }, {
        name       = [[extra_argument]],
        body       = [[function(tuple1, tuple2) assert(tuple2 == nil) return tuple1 end]],
        blocking   = false,
    }, {
        name       = [[returns_not_tuple]],
        body       = [[function() return 1 end]],
        errmsg     = [[Upgrade of space t failed: type of return value is expected to be array]],
        blocking   = true,
    }, {
        name       = [[non_deterministic]],
        body       = [[function(tuple) return tuple end]],
        errmsg     = [[Upgrade of space t failed: upgrade function is not normalized]],
        blocking   = false,
        is_deterministic = false,
    }, {
        name       = [[non_sandboxed]],
        body       = [[function(tuple) return tuple end]],
        errmsg     = [[Upgrade of space t failed: upgrade function is not normalized]],
        blocking   = false,
        is_sandboxed = false,
    }, {
        name = [[wrong_format_type]],
        body = [[
                function(tuple)
                    local new_tuple = {}
                    new_tuple[1] = tuple[1]
                    new_tuple[2] = tuple[2]
                    new_tuple[3] = 666
                    return new_tuple
                end
               ]],
        errmsg = [[Tuple field 3 (f3) type does not match one required by operation: expected string, got unsigned]],
        blocking = true
    }, {
        name = [[missing_field]],
        body = [[
                function(tuple)
                    local new_tuple = {}
                    new_tuple[1] = tuple[1]
                    new_tuple[2] = tuple[2]
                    return new_tuple
                end
               ]],
        errmsg = [[Tuple field 3 (f3) required by space format is missing]],
        blocking = true
    },{
        name = [[pk_modification]],
        body = [[
                function(tuple)
                    local new_tuple = {}
                    new_tuple[1] = tuple[1] + 1
                    new_tuple[2] = tuple[2]
                    new_tuple[3] = tuple[3]
                    return new_tuple
                end
               ]],
        errmsg = [[Attempt to modify a tuple field which is part of index 'pk' in space 't']],
        -- In test/notest modes error is different..
        errmsg_notest = "Duplicate key exists in unique index \"pk\" in space \"t\" with old tuple - "..
                        "[2, 3, \"b\"] and new tuple - [2, 2, \"a\"]",
        blocking = true,
    }, {
        name = [[sc_not_unique]],
        body = [[
                function(tuple)
                    local new_tuple = {}
                    new_tuple[1] = tuple[1]
                    new_tuple[2] = tuple[2] + 1
                    new_tuple[3] = tuple[3]
                    return new_tuple
                end
               ]],
        errmsg = "Duplicate key exists in unique index \"sc\" in space \"t\" with old tuple - "..
                 "[2, 3, \"b\"] and new tuple - [1, 3, \"a\"]",
        blocking = true,
    }
}

-- Let's define these names since we are going to use them quite frequently.
--
local func_noop = [[noop]]
local data_upgrade =  [[data_upgrade]]

-- Not all functions are deterministic but we are aware what we are doing..
--
local good_functions = {
    -- name, body, new_format
    {
        name   = func_noop,
        body   = [[function(tuple) return tuple end]],
    }, {
        name   = data_upgrade,
        body   = [[
                  function(tuple)
                      local new_tuple = {}
                      new_tuple[1] = tuple[1]
                      new_tuple[2] = tuple[2]
                      new_tuple[3] = tostring(tuple[2])
                      return new_tuple
                  end
               ]],
    }, {
        name   = [[change_type]],
        body   = [[
                  function(tuple)
                      local new_tuple = {}
                      new_tuple[1] = tuple[1]
                      new_tuple[2] = tuple[2]
                      new_tuple[3] = tuple[2]
                    return new_tuple
                  end
                 ]],
        new_format = {{name="f1", type="unsigned"},
                      {name="f2", type="unsigned"},
                      {name="f3", type="unsigned"}},
    }, {
        name   = [[add_field]],
        body   = [[
                  function(tuple)
                      local new_tuple = {}
                      new_tuple[1] = tuple[1]
                      new_tuple[2] = tuple[2]
                      new_tuple[3] = tuple[3]
                      new_tuple[4] = 'new_field'
                      return new_tuple
                  end
                 ]],
    }, {
        name   = [[remove_field]],
        body = [[
                function(tuple)
                    local new_tuple = {}
                    new_tuple[1] = tuple[1]
                    new_tuple[2] = tuple[2]
                    return new_tuple
                end
               ]],
        new_format = {{name="f1", type="unsigned"}, {name="f2", type="unsigned"}},
    }
}

local common_functions = {
    -- name, body
    {
        name   = "partial_upgrade",
        body   = [[
                  function(tuple)
                      if tuple[1] >= 2000 then error("boom") end
                      local new_tuple = {}
                      new_tuple[1] = tuple[1]
                      new_tuple[2] = tuple[2] + 1
                      return new_tuple
                  end
                 ]],
    }, {
        name   = "full_upgrade",
        body   = [[
                  function(tuple)
                      local new_tuple = {}
                      new_tuple[1] = tuple[1]
                      new_tuple[2] = tuple[2] + 1
                      return new_tuple
                  end
                 ]],
    },
}

-- Create several functions affecting differently. Firstly define
-- functions with explicit run-time errors, ones which do not correspond
-- to upgrade function requerements (are not persistent,
-- non-deterministic etc) and those which uprade tuples in the wrong way.
--
local function create_bad_functions()
    for _, v in pairs(bad_functions) do
        local options = {}
        local is_deterministic = true
        local is_sandboxed = true
        if v['is_deterministic'] ~= nil then
            is_deterministic = v['is_deterministic']
        end
        if v['is_sandboxed'] ~= nil then
            is_sandboxed = v['is_sandboxed']
        end
        options['body'] = v['body']
        options['is_deterministic'] = is_deterministic
        options['is_sandboxed'] = is_sandboxed
        box.schema.func.create(v['name'], options)
    end
end

local function create_good_functions()
    for _, v in pairs(good_functions) do
        box.schema.func.create(v['name'], {body = v['body'], is_deterministic = true, is_sandboxed = true})
    end
    for _, v in pairs(common_functions) do
        box.schema.func.create(v['name'], {body = v['body'], is_deterministic = true, is_sandboxed = true})
    end
end

local function cleanup_functions()
    for _, v in pairs(bad_functions) do
        box.schema.func.drop(v['name'])
    end
    for _, v in pairs(good_functions) do
        box.schema.func.drop(v['name'])
    end
    for _, v in pairs(common_functions) do
        box.schema.func.drop(v['name'])
    end
end

local function check_space_upgrade_empty()
    local cnt = box.space._space_upgrade.index[0]:count()
    test:is(cnt, 0, "No entries after running upgrade")
    if box.space.t ~= nil then
        test:is(box.space.t.upgrade_state, nil, "No upgrade is in progress")
    end
end

local function check_space_data(expected, count)
    local res = box.space.t:select()
    local res_converted = {}
    for i = 1, count do
        res_converted[i] = res[i]:totable()
    end
    test:is_deeply(res_converted, expected, "Data is consistent after upgrade")
end

local function finalize()
    cleanup_functions()
    box.space.t:drop()
    box.space.big:drop()
    check_space_upgrade_empty()
end

local function setup()
    box.cfg{}
    -- Create and fill in casual space to test basic capabilities.
    local t = box.schema.create_space("t")
    t:create_index("pk")
    t:create_index("sc", {parts = {2, "integer"}, unique = true})
    t:format({{"f1", "unsigned"}, {"f2", "unsigned"}, {"f3", "string"}})
    t:insert{1, 2, "a"}
    t:insert{2, 3, "b"}
    t:insert{3, 4, "c" }

    -- Create big space with many rows to check that yields/row commits
    -- work fine.
    --
    local b = box.schema.create_space("big")
    b:create_index("pk")
    b:format({{"f1", "unsigned"}, {"f2", "unsigned"}})
    local rows_cnt = 10000
    for i = 0, rows_cnt do
        b:replace({i, 0})
    end

    create_bad_functions()
    create_good_functions()
end

-- Check that :upgrade arguments are verified correctly.
local function check_args()
    local ok, res = pcall(box.space.t.upgrade, box.space.t, {mode = "asd", func = func_noop})
    test:is_deeply({ok, tostring(res)},
                   {false, "Illegal parameters, upgrade mode should be 'test|notest|upgrade' but got asd"},
                   "Arguments are checked correctly")
    ok, res = pcall(box.space.t.upgrade, box.space.t, {mode = "test"})
    test:is_deeply({ok, tostring(res)}, {false, "Illegal parameters, upgrade function is missing (nil)"},
                   "Arguments are checked correctly")
    ok, res = pcall(box.space.t.upgrade, box.space.t, {"test"})
    test:is_deeply({ok, tostring(res)}, {false, "Illegal parameters, unexpected option '1'"},
                   "Arguments are checked correctly")
    ok, res = pcall(box.space.t.upgrade, box.space.t, "test")
    test:is_deeply({ok, tostring(res)}, {false, "Illegal parameters, options should be a table"},
                   "Arguments are checked correctly")
    ok, res = pcall(box.space.t.upgrade, box.space.t)
    test:is_deeply({ok, tostring(res)}, {false, "Illegal parameters, upgrade mode is missing (nil)"},
                   "Arguments are checked correctly")
    ok, res = pcall(box.space.t.upgrade, box.space.t, {mode = "test", func = func_noop, format = 1})
    test:is_deeply({ok, tostring(res)},
                   {false, "Illegal parameters, options parameter 'format' should be of type table"},
                   "Arguments are checked correctly")
    ok, res = pcall(box.space.t.upgrade, box.space.t, {mode = "test", func = func_noop,  format = {1, 2, 3}})
    test:is_deeply({ok, tostring(res)}, {false, "Illegal parameters, format[1]: name (string) is expected"},
                   "Arguments are checked correctly")
end

local function upgrade_with_bad_functions(mode)
    test:diag("Test upgrade with 'bad' functions in mode "..mode)
    assert(mode == 'test' or mode == 'notest')
    for _, v in pairs(bad_functions) do
        local func = v['name']
        local errmsg = v['errmsg']
        local ok, res = pcall(box.space.t.upgrade, box.space.t, {mode = mode, func = func})
        if errmsg ~= nil then
            if mode == 'notest' and v['errmsg_notest'] ~= nil then
                errmsg = v['errmsg_notest']
            end
            test:is_deeply({ok, tostring(res)}, {false, errmsg}, "Proper error is returned for "..func)
        else
            test:is_deeply({ok, res}, {true, nil}, "No error is raised for "..func)
        end
        -- Restart upgrade with noop function in case of persistent error -
        -- no data changes with given functions should be made (bad functions
        -- does not affect tuples).
        --
        if mode == 'notest' and v['blocking'] then
            ok, res = pcall(box.space.t.upgrade, box.space.t, {mode = mode, func = func_noop})
            test:is_deeply({ok, res}, {true, nil}, "No error is raised after restoring upgrade "..func)
        end
    end
    -- Also make sure that there's no any entries in _space_upgrade:
    --
    check_space_upgrade_empty()
end

local function upgrade_with_good_functions(mode)
    test:diag("Test upgrade with 'good' functions in mode "..mode)
    assert(mode == 'test' or mode == 'notest')
    for _, v in pairs(good_functions) do
        local func = v['name']
        local new_format = v['new_format']
        local ok, res = pcall(box.space.t.upgrade, box.space.t, {mode = mode, func = func, format = new_format})
        test:is_deeply({ok, res}, {true, nil}, "No error is raised for "..func)
    end
    check_space_upgrade_empty()
    if mode == 'notest' then
        check_space_data({{1, 2}, {2, 3}, {3, 4}}, 3)
    end
end

-- Consider following scenario:
-- 1. Space has format {unsigned, unsigned};
-- 2. Set new format to {unsigned, unsigned, string};
-- 3. Set upgrade function to noop (i.e. it returns tuples in format {unsigned, unsigned});
-- 4. Upgrade changes state to "error";
-- 5. Try to insert tuple which does not correspond to the new format;
-- 6. Try to insert tuple which fits into new format;
-- 7. Set correct upgrade function and finish recovery;
-- 8. Check data consistency.
--
local function upgrade_with_format_change()
    test:diag("Test new format applies immediately")
    local new_format  = {{name="f1", type="unsigned"},
                         {name="f2", type="unsigned"},
                         {name="x",  type="string"} }
    -- Steps 2,3: noop function should transfer upgrade to error mode.
    local ok, res = pcall(box.space.t.upgrade, box.space.t, {mode = "notest", func = "noop", format = new_format})
    local errmsg = "Tuple field 3 (x) required by space format is missing"
    --Step 4:
    test:is_deeply({ok, tostring(res)}, {false, errmsg}, "Proper error is returned")
    -- Step 5: {5} does not correspond to string field type so this replace should fail.
    ok, res = pcall(box.space.t.replace, box.space.t, {5, 5, 5})
    errmsg = "Tuple field 3 (x) type does not match one required by operation: expected string, got unsigned"
    test:is_deeply({ok, tostring(res)}, {false, errmsg}, "Proper error is returned for replace")
    -- Step 6:
    local ok, _ = pcall(box.space.t.replace, box.space.t, {5, 5, "asd"})
    test:is(ok, true, "Processed replaced during upgrade")
    -- Step 7:
    ok, res = pcall(box.space.t.upgrade, box.space.t, {mode = "notest", func = data_upgrade})
    test:is_deeply({ok, res}, {true, nil}, "Upgrade has successfully finished")
    -- Step 8:
    -- "asd" in the last tuple must be converted to "5"
    check_space_data({{1, 2, '2'}, {2, 3, '3'}, {3, 4, '4'}, {5, 5, "5"}}, 4)
    check_space_upgrade_empty()
end

local function upgrade_of_big_space()
    test:diag("Test long upgrade")
    -- Initially data in big space is in form of {i, 0}. Since we are executing
    -- upgrade not in a separate fiber, we wait until it fails.
    local _, res = pcall(box.space.big.upgrade, box.space.big, {mode = "notest", func = "partial_upgrade"})
    test:isnumber(string.find(tostring(res), "boom"), "Upgrade function failed")
    local updated_row = box.space.big.index[0]:get({1999})
    local skipped_row = box.space.big.index[0]:get({2000})
    test:is(updated_row[2], 1, "Row is updated")
    test:is(skipped_row[2], 0, "Row is skipped")
    -- Check also entry in _space_upgrade:
    local upgrade_tuple = box.space._space_upgrade.index[0]:get({box.space.big.id})
    test:is(upgrade_tuple[2], "error", "Entry in _space_upgrade has proper status")
    test:is(box.space.big.upgrade_state.status, "error", "upgrade_state contains proper status")

    -- Let's check that space can't be dropped util upgrade is finished.
    local ok, res = pcall(box.space.big.drop, box.space.big)
    test:is_deeply({ok, tostring(res)}, {false,  [[Can't modify space 'big': upgrade of space is in-progress]]},
                   "Space can't be dropped")
    local ok, res = pcall(box.space.big.index[0].drop, box.space.big.index[0])
    test:is_deeply({ok, tostring(res)}, {false,  [[Can't modify space 'big': upgrade of space is in-progress]]},
        "Space can't be dropped")

    local _, res = pcall(box.space.big.upgrade, box.space.big, {mode = "notest", func = "full_upgrade"})
    test:is(res, nil, "Upgrade has been finished")
    updated_row = box.space.big.index[0]:get({1999})
    skipped_row = box.space.big.index[0]:get({10000})
    test:is(updated_row[2], 2, "Row is updated twice")
    test:is(skipped_row[2], 1, "Row is updated once")
    check_space_upgrade_empty()
end

local function run_all()
    setup()
    check_args()
    local modes = {'test', 'notest' }
    for _, v in pairs(modes) do
        upgrade_with_bad_functions(v)
        upgrade_with_good_functions(v)
    end
    upgrade_with_format_change()
    upgrade_of_big_space()
    finalize()
end

run_all()

os.exit(test:check() and 0 or 1)
