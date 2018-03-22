#!/usr/bin/env tarantool

local ffi      = require('ffi')
local fio      = require('fio')
local tap      = require('tap')
local uuid     = require('uuid')
local yaml     = require('yaml')
local errno    = require('errno')
local fiber    = require('fiber')
local test_run = require('test_run').new()

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
        print(string.format('!!! failed to rmdir path "%s"', file))
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
tctlcfg_code = [[default_cfg = {
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

local function tctl_wait(dir, name)
    if name then
        local path = fio.pathjoin(dir, name .. '.control')
        while not fio.stat(path) do
            fiber.sleep(0.01)
        end
        ::again::
        while true do
            local stat, nb = pcall(require('net.box').new, path, {
                wait_connected = true, console = true
            })
            if stat == false then
                fiber.sleep(0.01)
                goto again
            else
                break
            end
            local stat, msg = pcall(nb.eval, nb, 'require("fiber").time()')
            if stat == false then
                fiber.sleep(0.01)
            else
                break
            end
        end
    end
end

local function tctl_command(dir, cmd, args, name)
    local pid = nil
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
        local val = test:is(res, e_res, ("check '%s' stdout for '%s'"):format(cmd,args))
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

local test = tap.test('tarantoolctl')
test:plan(6)

-- basic start/stop test
-- must be stopped afterwards
do
    local dir = fio.tempdir()
    local code = [[ box.cfg{memtx_memory = 104857600} ]]
    create_script(dir, 'script.lua', code)

    local status, err = pcall(function()
        test:test("basic test", function(test_i)
            test_i:plan(16)
            check_ok(test_i, dir, 'start',  'script', 0, nil, "Starting instance")
            tctl_wait(dir, 'script')
            check_ok(test_i, dir, 'status', 'script', 0, nil, "is running")
            check_ok(test_i, dir, 'start',  'script', 1, nil, "is already running")
            check_ok(test_i, dir, 'status', 'script', 0, nil, "is running")
            check_ok(test_i, dir, 'stop',   'script', 0, nil, "Stopping")
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
            test_i:plan(8)
            check_ok(test_i, dir, 'start', 'script', 1, nil,
                     'Instance script is not found')
            check_ok(test_i, dir, 'start', 'bad_script', 1, nil,
                     'unexpected symbol near')
            check_ok(test_i, dir, 'start', 'good_script', 0)
            tctl_wait(dir, 'good_script')
            -- wait here
            check_ok(test_i, dir, 'eval',  'good_script bad_script.lua', 3,
                     nil, 'Error while reloading config:')
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
            test_i:plan(6)
            check_ok(test_i, dir, 'start', 'good_script', 0)
            tctl_wait(dir, 'good_script')
            check_ok(test_i, dir, 'eval',  'good_script bad_script.lua', 3, nil,
                     'Error while reloading config')
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

-- check basic help
do
    local dir = fio.tempdir()

    local function test_help(test, dir, cmd, e_stderr)
        local desc = dir and 'with config' or 'without config'
        dir = dir or './'
        local res, stdout, stderr = run_command(dir, cmd)
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
        os.exit(0)
    ]]

    create_script(dir, 'filler.lua', filler_code)

    local function check_ctlcat_xlog(test, dir, args, delim, lc)
        local command_base = 'tarantoolctl cat filler/00000000000000000000.xlog'
        local desc = args and "cat + " .. args or "cat"
        args = args and " " .. args or ""
        local res, stdout, stderr = run_command(dir, command_base .. args)
        test:is(res, 0, desc .. " result")
        test:is(select(2, stdout:gsub(delim, delim)), lc, desc .. " line count")
    end

    local function check_ctlcat_snap(test, dir, args, delim, lc)
        local command_base = 'tarantoolctl cat filler/00000000000000000000.snap'
        local desc = args and "cat + " .. args or "cat"
        args = args and " " .. args or ""
        local res, stdout, stderr = run_command(dir, command_base .. args)
        test:is(res, 0, desc .. " result")
        test:is(select(2, stdout:gsub(delim, delim)), lc, desc .. " line count")
    end

    local status, err = pcall(function()
        test:test("fill and test cat output", function(test_i)
            test_i:plan(29)
            check_ok(test_i, dir, 'start', 'filler', 0)
            check_ctlcat_xlog(test_i, dir, nil, "---\n", 6)
            check_ctlcat_xlog(test_i, dir, "--space=512", "---\n", 6)
            check_ctlcat_xlog(test_i, dir, "--space=666", "---\n", 0)
            check_ctlcat_xlog(test_i, dir, "--show-system", "---\n", 9)
            check_ctlcat_xlog(test_i, dir, "--format=json", "\n", 6)
            check_ctlcat_xlog(test_i, dir, "--format=lua",  "\n", 6)
            check_ctlcat_xlog(test_i, dir, "--from=3 --to=6 --format=json", "\n", 2)
            check_ctlcat_xlog(test_i, dir, "--from=3 --to=6 --format=json --show-system", "\n", 3)
            check_ctlcat_xlog(test_i, dir, "--from=6 --to=3 --format=json --show-system", "\n", 0)
            check_ctlcat_xlog(test_i, dir, "--from=3 --to=6 --format=json --show-system --replica 1", "\n", 3)
            check_ctlcat_xlog(test_i, dir, "--from=3 --to=6 --format=json --show-system --replica 1 --replica 2", "\n", 3)
            check_ctlcat_xlog(test_i, dir, "--from=3 --to=6 --format=json --show-system --replica 2", "\n", 0)
            check_ctlcat_snap(test_i, dir, "--space=280", "---\n", 18)
            check_ctlcat_snap(test_i, dir, "--space=288", "---\n", 43)
        end)
    end)

    recursive_rmdir(dir)

    if status == false then
        print(("Error: %s"):format(err))
        os.exit()
    end
end

-- check play
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
    local port = tonumber(
        test_run:eval("remote",
                      "return require('uri').parse(box.cfg.listen).service")[1]
    )

    local command_base = ('tarantoolctl play localhost:%d filler/00000000000000000000.xlog'):format(port)

    local status, err = pcall(function()
        test:test("fill and test play output", function(test_i)
            test_i:plan(6)
            check_ok(test_i, dir, 'start', 'filler', 0)
            local lsn_before = test_run:get_lsn("remote", 1)
            test_i:is(lsn_before, 4, "check lsn before")
            local res, stdout, stderr = run_command(dir, command_base)
            test_i:is(res, 0, "execution result")
            test_i:is(test_run:get_lsn("remote", 1), 10, "check lsn after")
            local res, stdout, stderr = run_command(dir, command_base)
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

os.exit(test:check() == true and 0 or -1)
