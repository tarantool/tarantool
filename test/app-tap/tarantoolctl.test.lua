#!/usr/bin/env tarantool

local ffi      = require('ffi')
local fio      = require('fio')
local tap      = require('tap')
local uuid     = require('uuid')
local errno    = require('errno')
local fiber    = require('fiber')
local ok, test_run = pcall(require, 'test_run')
test_run = ok and test_run.new() or nil

local BUILDDIR = os.getenv('BUILDDIR') or '.'
local TARANTOOLCTL_PATH = ('%s/extra/dist/tarantoolctl'):format(BUILDDIR)

local function recursive_rmdir(path)
    path = fio.abspath(path)
    local path_content = fio.glob(fio.pathjoin(path, '*'))
    for _, val in ipairs(fio.glob(fio.pathjoin(path, '.*'))) do
        if fio.basename(val) ~= '.' and fio.basename(val) ~= '..' then
            table.insert(path_content, val)
        end
    end
    for _, file in ipairs(path_content) do
        local stat = fio.stat(file)
        if stat:is_dir() then
            recursive_rmdir(file)
        else
            if fio.unlink(file) == false then
                print(string.format('!!! failed to unlink file "%s"', file))
                print(string.format('!!! [errno %s]: %s', errno(), errno.strerror()))
            end
        end
    end
    if fio.rmdir(path) == false then
        print(string.format('!!! failed to rmdir path "%s"', path))
        print(string.format('!!! [errno %s]: %s', errno(), errno.strerror()))
    end
end

ffi.cdef[[
typedef int32_t pid_t;
int kill(pid_t pid, int sig);

/* For execution with background == false */
pid_t fork(void);
int open(const char *pathname, int flags, int mode);
int close(int fd);
int dup2(int oldfd, int newfd);
int execvp(const char *file, char *const argv[]);
]]

-- background checks
local tctlcfg_code = [[default_cfg = {
    pid_file  = '.', wal_dir = '.', memtx_dir   = '.' ,
    vinyl_dir = '.', log  = '.', background = true,
}

instance_dir = require('fio').abspath('.')]]

local function cleanup_instance(dir, name)
    local pid = io.open(fio.pathjoin(dir, name .. ".pid"))
    if pid ~= nil then
        pid = tonumber(pid:read("*a"))
    end
    if pid ~= nil then
        ffi.C.kill(pid, 9)
    end
end

local function create_script(dir, name, code)
    local path = fio.pathjoin(dir, name)
    local script = fio.open(path, {'O_CREAT', 'O_WRONLY'},
        tonumber('0777', 8))
    assert(script ~= nil, ("assertion: Failed to open '%s' for writing"):format(path))
    script:write(code)
    script:close()
    return path
end

local function run_command(dir, command)
    local suffix = uuid.str():sub(1, 8)
    local fstdout = fio.pathjoin(dir, 'stdout-' .. suffix)
    local fstderr = fio.pathjoin(dir, 'stderr-' .. suffix)
    local line = [[/bin/sh -c 'cd "%s" && %s >"%s" 2>"%s"']]
    line = line:format(dir, command, fstdout, fstderr)
    local res = os.execute(line)
    local fstdout_e, fstderr_e = io.open(fstdout):read('*a'), io.open(fstderr):read('*a')
    fio.unlink(fstdout); fio.unlink(fstderr);
    return res/256, fstdout_e, fstderr_e
end

local function tctl_wait_start(dir, name)
    if name then
        local path = fio.pathjoin(dir, name .. '.control')
        while not fio.stat(path) do
            fiber.sleep(0.01)
        end
        ::again::
        local stat, _ = pcall(require('net.box').new, path, {
            wait_connected = true, console = true
        })
        if stat == false then
            fiber.sleep(0.01)
            goto again
        end
    end
end

local function tctl_wait_stop(dir, name)
    local path = fio.pathjoin(dir, name .. '.pid')
    while fio.path.exists(path) do
        fiber.sleep(0.01)
    end
end

local function tctl_command(dir, cmd, args)
    if not fio.stat(fio.pathjoin(dir, '.tarantoolctl')) then
        create_script(dir, '.tarantoolctl', tctlcfg_code)
    end
    local command = ('tarantoolctl %s %s'):format(cmd, args)
    return run_command(dir, command)
end

local function check_ok(test, dir, cmd, args, e_res, e_stdout, e_stderr)
    local res, stdout, stderr = tctl_command(dir, cmd, args)
    stdout, stderr = stdout or '', stderr or ''
    local ares = true
    if (e_res ~= nil) then
        local val = test:is(res, e_res, ("check '%s' command status for '%s'"):format(cmd,args))
        ares = ares and val
    end
    if e_stdout ~= nil then
        local val = test:ok(stdout:find(e_stdout), ("check '%s' stdout for '%s'"):format(cmd,args))
        ares = ares and val
        if not val then
            print(("Expected to find '%s' in '%s'"):format(e_stdout, stdout))
        end
    end
    if e_stderr ~= nil then
        local val = test:ok(stderr:find(e_stderr), ("check '%s' stderr for '%s'"):format(cmd,args))
        ares = ares and val
        if not val then
            print(("Expected to find '%s' in '%s'"):format(e_stderr, stderr))
        end
    end
    if not ares then
        print(res, stdout, stderr)
    end
end

local function merge(...)
    local res = {}
    for i = 1, select('#', ...) do
        local t = select(i, ...)
        for k, v in pairs(t) do
            res[k] = v
        end
    end
    return res
end

local test = tap.test('tarantoolctl')
test:plan(8)

-- basic start/stop test
-- must be stopped afterwards
do
    local dir = fio.tempdir()
    create_script(dir, 'script.lua', [[ box.cfg{memtx_memory = 104857600} ]])
    create_script(dir, 'delayed_box_cfg.lua', [[
        local fiber = require('fiber')
        fiber.create(function()
            fiber.sleep(1)
            box.cfg{}
        end)
    ]])

    local status, err = pcall(function()
        test:test("basic test", function(test_i)
            test_i:plan(20)
            local script = 'delayed_box_cfg'
            check_ok(test_i, dir, 'start', script, 0, nil, "Starting instance")
            tctl_wait_start(dir, script)
            check_ok(test_i, dir, 'stop', script, 0, nil, "Stopping")
            tctl_wait_stop(dir, script)

            check_ok(test_i, dir, 'start',  'script', 0, nil, "Starting instance")
            tctl_wait_start(dir, 'script')
            check_ok(test_i, dir, 'status', 'script', 0, nil, "is running")
            check_ok(test_i, dir, 'start',  'script', 1, nil, "is already running")
            check_ok(test_i, dir, 'status', 'script', 0, nil, "is running")
            check_ok(test_i, dir, 'stop',   'script', 0, nil, "Stopping")
            tctl_wait_stop(dir, 'script')
            check_ok(test_i, dir, 'status', 'script', 1, nil, "is stopped")
            check_ok(test_i, dir, 'stop',   'script', 0, nil, "is not running")
            check_ok(test_i, dir, 'status', 'script', 1, nil, "is stopped" )
        end)
    end)

    cleanup_instance(dir, 'script')
    recursive_rmdir(dir)

    if status == false then
        print(("Error: %s"):format(err))
        os.exit()
    end
end

-- check sandboxes
do
    local dir = fio.tempdir()
    -- bad code
    local code = [[ box.cfg{ ]]
    create_script(dir, 'bad_script.lua',  code)
    local code = [[ box.cfg{memtx_memory = 104857600} ]]
    create_script(dir, 'good_script.lua', code)

    local status, err = pcall(function()
        test:test("basic test for bad script", function(test_i)
            test_i:plan(7)
            check_ok(test_i, dir, 'start', 'script', 1, nil,
                     'Instance script is not found')
            check_ok(test_i, dir, 'start', 'bad_script', 1, nil,
                     'unexpected symbol near')
            check_ok(test_i, dir, 'start', 'good_script', 0)
            tctl_wait_start(dir, 'good_script')
            -- wait here
            check_ok(test_i, dir, 'eval',  'good_script bad_script.lua', 3,
                     nil, nil)
            check_ok(test_i, dir, 'stop', 'good_script', 0)
        end)
    end)

    cleanup_instance(dir, 'good_script')
    recursive_rmdir(dir)

    if status == false then
        print(("Error: %s"):format(err))
        os.exit()
    end
end

-- check answers in case of eval
do
    local dir = fio.tempdir()
    -- bad code
    local code = [[ error('help'); return 1]]
    create_script(dir, 'bad_script.lua',  code)
    local code = [[ return 1]]
    create_script(dir, 'ok_script.lua',  code)
    local code = [[ box.cfg{memtx_memory = 104857600} box.once('help', function() end)]]
    create_script(dir, 'good_script.lua', code)

    local status, err = pcall(function()
        test:test("check answers in case of call", function(test_i)
            test_i:plan(5)
            check_ok(test_i, dir, 'start', 'good_script', 0)
            tctl_wait_start(dir, 'good_script')
            check_ok(test_i, dir, 'eval',  'good_script bad_script.lua', 3,
                     nil, nil)
            check_ok(test_i, dir, 'eval',  'good_script ok_script.lua', 0,
                     '---\n- 1\n...', nil)
            check_ok(test_i, dir, 'stop', 'good_script', 0)
        end)
    end)

    cleanup_instance(dir, 'good_script')
    recursive_rmdir(dir)

    if status == false then
        print(("Error: %s"):format(err))
        os.exit()
    end
end

-- check enter
do
    local dir = fio.tempdir()

    local code = [[ box.cfg{} ]]
    create_script(dir, 'script.lua', code)

    local status, err = pcall(function()
        test:test("check error codes in case of enter", function(test_i)
            test_i:plan(10)
            check_ok(test_i, dir, 'enter', 'script', 1, nil, "Can't connect to")
            local console_sock = 'script.control'
            console_sock = fio.pathjoin(dir, console_sock)
            test_i:is(fio.path.exists(console_sock), false, "directory clean")
            check_ok(test_i, dir, 'start', 'script', 0)
            tctl_wait_start(dir, 'script')
            test_i:is(fio.path.exists(console_sock), true,
                      "unix socket created")
            check_ok(test_i, dir, 'stop', 'script', 0)
            tctl_wait_stop(dir, 'script')
            test_i:is(fio.path.exists(console_sock), false,
                      "remove unix socket upon exit")
            fio.open(console_sock, 'O_CREAT')
            test_i:is(fio.path.exists(console_sock), true, "file created")
            check_ok(test_i, dir, 'enter', 'script', 1, nil, "Can't connect to")
            fio.unlink(console_sock)
        end)
    end)

    cleanup_instance(dir, 'script')
    recursive_rmdir(dir)

    if status == false then
        print(("Error: %s"):format(err))
        os.exit()
    end
end

-- check basic help
do
    local dir = fio.tempdir()

    local function test_help(test, dir, cmd, e_stderr)
        local desc = dir and 'with config' or 'without config'
        dir = dir or './'
        local _, _, stderr = run_command(dir, cmd)
        if e_stderr ~= nil then
            if not test:ok(stderr:find(e_stderr), ("check stderr of '%s' %s"):format(cmd, desc)) then
                print(("Expected to find '%s' in '%s'"):format(e_stderr, stderr))
            end
        end
    end

    create_script(dir, '.tarantoolctl', tctlcfg_code)

    local status, err = pcall(function()
        test:test("check basic help", function(test_i)
            test_i:plan(4)
            test_help(test_i, nil, "tarantoolctl", "Usage:")
            test_help(test_i, nil, "tarantoolctl help", "Usage:")
            test_help(test_i, nil, "tarantoolctl --help", "Usage:")
            test_help(test_i, dir, "tarantoolctl", "Usage:")
        end)
    end)

    recursive_rmdir(dir)

    if status == false then
        print(("Error: %s"):format(err))
        os.exit()
    end
end

-- check cat
do
    local dir = fio.tempdir()

    local filler_code = [[
        box.cfg{memtx_memory = 104857600, background=false}
        local space = box.schema.create_space("test")
        space:create_index("primary")
        space:insert({[1] = 1, [2] = 2, [3] = 3, [4] = 4})
        space:replace({[1] = 2, [2] = 2, [3] = 3, [4] = 4})
        space:delete({[1] = 1})
        space:update({[1] = 2}, {[1] = {[1] = '\x3d', [2] = 3, [3] = 4}})
        space:upsert({[1] = 3, [2] = 4, [3] = 5, [4] = 6}, {[1] = {[1] = '\x3d', [2] = 3, [3] = 4}})
        space:upsert({[1] = 3, [2] = 4, [3] = 5, [4] = 6}, {[1] = {[1] = '\x3d', [2] = 3, [3] = 4}})
        f = function(old, new) return old end
        space:before_replace(f)
        space:replace{1,5}
        space:before_replace(nil, f)
        os.exit(0)
    ]]

    create_script(dir, 'filler.lua', filler_code)

    local function check_ctlcat_xlog(test, dir, args, delim, lc)
        local command_base = 'tarantoolctl cat filler/00000000000000000000.xlog'
        local desc = args and "cat + " .. args or "cat"
        args = args and " " .. args or ""
        local res, stdout, _ = run_command(dir, command_base .. args)
        test:is(res, 0, desc .. " result")
        test:is(select(2, stdout:gsub(delim, delim)), lc, desc .. " line count")
    end

    local function check_ctlcat_snap(test, dir, args, delim, lc)
        local command_base = 'tarantoolctl cat filler/00000000000000000000.snap'
        local desc = args and "cat + " .. args or "cat"
        args = args and " " .. args or ""
        local res, stdout, _ = run_command(dir, command_base .. args)
        test:is(res, 0, desc .. " result")
        test:is(select(2, stdout:gsub(delim, delim)), lc, desc .. " line count")
    end

    local status, err = pcall(function()
        test:test("fill and test cat output", function(test_i)
            test_i:plan(29)
            check_ok(test_i, dir, 'start', 'filler', 0)
            check_ctlcat_xlog(test_i, dir, nil, "---\n", 7)
            check_ctlcat_xlog(test_i, dir, "--space=512", "---\n", 6)
            check_ctlcat_xlog(test_i, dir, "--space=666", "---\n", 0)
            check_ctlcat_xlog(test_i, dir, "--show-system", "---\n", 10)
            check_ctlcat_xlog(test_i, dir, "--format=json", "\n", 7)
            check_ctlcat_xlog(test_i, dir, "--format=lua",  "\n", 6)
            check_ctlcat_xlog(test_i, dir, "--from=3 --to=6 --format=json", "\n", 2)
            check_ctlcat_xlog(test_i, dir, "--from=3 --to=6 --format=json --show-system", "\n", 3)
            check_ctlcat_xlog(test_i, dir, "--from=6 --to=3 --format=json --show-system", "\n", 0)
            check_ctlcat_xlog(test_i, dir, "--from=3 --to=6 --format=json --show-system --replica 1", "\n", 3)
            check_ctlcat_xlog(test_i, dir,
		"--from=3 --to=6 --format=json --show-system --replica 1 --replica 2", "\n", 3)
            check_ctlcat_xlog(test_i, dir, "--from=3 --to=6 --format=json --show-system --replica 2", "\n", 0)
            check_ctlcat_snap(test_i, dir, "--space=280", "---\n", 25)
            check_ctlcat_snap(test_i, dir, "--space=288", "---\n", 53)
        end)
    end)

    recursive_rmdir(dir)

    if status == false then
        print(("Error: %s"):format(err))
        os.exit()
    end
end

-- check play
if test_run == nil then
    test:skip('skip \'tarantoolctl play\' test (test-run is required)')
else
    local dir = fio.tempdir()

    local filler_code = [[
        box.cfg{memtx_memory = 104857600, background=false}
        local space = box.schema.create_space("test")
        space:create_index("primary")
        space:insert({[1] = 1, [2] = 2, [3] = 3, [4] = 4})
        space:replace({[1] = 2, [2] = 2, [3] = 3, [4] = 4})
        space:delete({[1] = 1})
        space:update({[1] = 2}, {[1] = {[1] = '\x3d', [2] = 3, [3] = 4}})
        space:upsert({[1] = 3, [2] = 4, [3] = 5, [4] = 6}, {[1] = {[1] = '\x3d', [2] = 3, [3] = 4}})
        space:upsert({[1] = 3, [2] = 4, [3] = 5, [4] = 6}, {[1] = {[1] = '\x3d', [2] = 3, [3] = 4}})
        f = function(old, new) return old end
        space:before_replace(f)
        space:replace{1,5}
        space:before_replace(nil, f)
        os.exit(0)
    ]]
    create_script(dir, 'filler.lua', filler_code)

    local remote_code = [[
        box.cfg{
            listen = os.getenv("LISTEN"),
            memtx_memory = 104857600
        }
        local space = box.schema.create_space("test")
        space:create_index("primary")
        box.schema.user.grant("guest", "read,write", "space", "test")
        require('console').listen(os.getenv("ADMIN"))
    ]]
    local remote_path = create_script(dir, 'remote.lua', remote_code)
    test_run:cmd(("create server remote with script='%s'"):format(remote_path))
    test_run:cmd("start server remote")
    local listen_uri = test_run:eval("remote", "return box.cfg.listen")[1]

    local command_base = ('tarantoolctl play %s filler/00000000000000000000.xlog'):format(listen_uri)

    local status, err = pcall(function()
        test:test("fill and test play output", function(test_i)
            test_i:plan(6)
            check_ok(test_i, dir, 'start', 'filler', 0)
            local lsn_before = test_run:get_lsn("remote", 1)
            test_i:is(lsn_before, 4, "check lsn before")
            local res, _, _ = run_command(dir, command_base)
            test_i:is(res, 0, "execution result")
            test_i:is(test_run:get_lsn("remote", 1), 10, "check lsn after")
            local res, _, _ = run_command(dir, command_base)
            test_i:is(res, 0, "execution result")
            test_i:is(test_run:get_lsn("remote", 1), 16, "check lsn after")
        end)
    end)

    test_run:cmd("stop server remote")
    test_run:cmd("cleanup server remote")
    recursive_rmdir(dir)

    if status == false then
        print(("Error: %s"):format(err))
        os.exit()
    end
end

test:test('filter_xlog', function(test)
    local xlog_data = {
        -- [1] =
        {
            HEADER = {lsn = 130, type = 'INSERT', timestamp = 1551987542.8702},
            BODY = {space_id = 515, tuple = {1}},
        },
        -- [2] =
        {
            HEADER = {lsn = 131, type = 'INSERT', timestamp = 1551987542.8702},
            BODY = {space_id = 515, tuple = {2}},
        },
        -- [3] =
        {
            HEADER = {lsn = 132, type = 'INSERT', timestamp = 1551987542.8702},
            BODY = {space_id = 515, tuple = {3}},
        },
        -- [4] =
        {
            HEADER = {lsn = 133, type = 'INSERT', timestamp = 1551987542.8702},
            BODY = {space_id = 516, tuple = {'a'}},
        },
        -- [5] =
        {
            HEADER = {lsn = 134, type = 'INSERT', timestamp = 1551987542.8702},
            BODY = {space_id = 517, tuple = {'a'}},
        },
        -- [6] =
        {
            HEADER = {lsn = 135, type = 'INSERT', timestamp = 1551987542.8702},
            BODY = {space_id = 518, tuple = {'a'}},
        },
        -- [7] =
        {
            HEADER = {lsn = 136, type = 'INSERT', timestamp = 1551987542.8702,
                      replica_id = 1},
            BODY = {space_id = 515, tuple = {4}},
        },
        -- [8] =
        {
            HEADER = {lsn = 137, type = 'INSERT', timestamp = 1551987542.8702,
                      replica_id = 1},
            BODY = {space_id = 515, tuple = {5}},
        },
        -- [9] =
        {
            HEADER = {lsn = 100, type = 'INSERT', timestamp = 1551987542.8702,
                      replica_id = 2},
            BODY = {space_id = 515, tuple = {6}},
        },
        -- [10] =
        {
            HEADER = {lsn = 110, type = 'INSERT', timestamp = 1551987542.8702,
                      replica_id = 3},
            BODY = {space_id = 515, tuple = {7}},
        },
        -- [11] =
        {
            HEADER = {lsn = 138, type = 'INSERT', timestamp = 1551987542.8702,
                      replica_id = 1},
            BODY = {space_id = 515, tuple = {8}},
        },
    }

    local default_opts = {
        from = 0,
        to = -1ULL,
        ['show-system'] = false,
    }

    local x = xlog_data -- alias
    local cases = {
        {
            'w/o args',
            opts = default_opts,
            exp_result = x,
        },
        {
            'from and to',
            opts = merge(default_opts, {from = 131, to = 132}),
            exp_result = {x[2]},
        },
        {
            'space id',
            opts = merge(default_opts, {space = {516}}),
            exp_result = {x[4]},
        },
        {
            'space ids',
            opts = merge(default_opts, {space = {516, 517}}),
            exp_result = {x[4], x[5]},
        },
        {
            'replica id',
            opts = merge(default_opts, {replica = {1}}),
            exp_result = {x[7], x[8], x[11]},
        },
        {
            'replica ids',
            opts = merge(default_opts, {replica = {1, 2}}),
            exp_result = {x[7], x[8], x[9], x[11]},
        },
        {
            'to w/o replica id',
            opts = merge(default_opts, {to = 120}),
            exp_result = {x[9], x[10]},
        },
        {
            'to and replica id',
            opts = merge(default_opts, {to = 137, replica = {1}}),
            exp_result = {x[7]},
        },
        {
            'to and replica ids',
            opts = merge(default_opts, {to = 137, replica = {1, 2}}),
            exp_result = {x[7], x[9]},
        },
    }
    test:plan(#cases)

    rawset(_G, 'TARANTOOLCTL_UNIT_TEST', true)
    local tarantoolctl = dofile(TARANTOOLCTL_PATH)

    -- Like xlog.pairs().
    local function gen(param)
        local row = param.data[param.idx]
        if row == nil then
            return
        end
        param.idx = param.idx + 1
        return row.HEADER.lsn, row
    end

    local function xlog_data_pairs(data)
        return gen, {data = data, idx = 1}, 0
    end

    for _, case in ipairs(cases) do
        local gen, param, state = xlog_data_pairs(xlog_data)
        local res = {}
        tarantoolctl.internal.filter_xlog(gen, param, state, case.opts,
            function(record)
                table.insert(res, record)
            end
        )
        test:is_deeply(res, case.exp_result, case[1])
    end
end)

os.exit(test:check() == true and 0 or -1)
