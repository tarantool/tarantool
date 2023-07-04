local t = require("luatest")
local popen = require("popen")
local fio = require("fio")
local g = t.group()

local msg_opt_processing = "entering the event loop"

local function tarantool_path(arg)
    local index = -2
    -- arg[-1] is guaranteed to be non-null
    while arg[index] do index = index - 1 end
    return arg[index + 1]
end

-- Check presence of string 'msg' in file 'file'.
-- Returns true when string is found and false otherwise.
-- Function is not as smart as grep_log in a luatest (luatest/server.lua)
-- and reads the whole log file every time, but this log file has a small
-- size so it is ok.
-- https://github.com/tarantool/luatest/blob/89da427f8bb3bb66e01d2a7b5a9370d0428d8c52/luatest/server.lua#L660-L727
local function check_err_msg(file, msg)
    local f = fio.open(file, {'O_RDONLY', 'O_NONBLOCK'})
    t.assert_not_equals(f, nil)
    local content = f:read(2048)
    f:close()
    return (string.match(content, msg) and true) or false
end

local TARANTOOL_PATH = tarantool_path(arg)

g.before_test("test_background_mode_box_cfg", function()
    g.work_dir = fio.tempdir()
    g.log_path = fio.pathjoin(g.work_dir, "tarantool.log")
    g.pid_path = fio.pathjoin(g.work_dir, "tarantool.pid")

    t.assert_equals(fio.path.exists(g.log_path), false)
    t.assert_equals(fio.path.exists(g.pid_path), false)

    local box_cfg = string.format([[-e box.cfg{
pid_file='%s', background=true, work_dir='%s', log='%s',
}]], g.pid_path, g.work_dir, g.log_path)
    local cmd = {
        TARANTOOL_PATH, box_cfg,
    }
    g.ph = popen.new(cmd, {
        stderr = popen.opts.PIPE,
        stdin = popen.opts.PIPE,
        stdout = popen.opts.PIPE,
    })

    -- Start Tarantool and check that at least a log file has been created.
    t.assert_is_not(g.ph, nil)
    t.helpers.retrying({timeout = 2, delay = 0.01}, function(path)
        assert(fio.path.exists(path) == true)
    end, g.log_path)
    g.pid = fio.open(g.pid_path):read()
end)

g.after_test("test_background_mode_box_cfg", function(cg)
    os.execute("kill -9 " .. cg.pid)
    fio.unlink(cg.pid_path)
    fio.unlink(cg.log_path)
    os.remove("*.xlog")
    os.remove("*.snap")
end)

g.test_background_mode_box_cfg = function(cg)
    t.helpers.retrying({timeout = 2, delay = 0.01}, function()
        assert(check_err_msg(cg.log_path, msg_opt_processing) == true)
    end, cg.log_path, cg.ph)
end

g.before_test("test_background_mode_env_vars", function()
    local cmd = { TARANTOOL_PATH, "-e",  "box.cfg{}" }
    g.work_dir = fio.tempdir()
    g.log_path = fio.pathjoin(g.work_dir, "tarantool.log")
    g.pid_path = fio.pathjoin(g.work_dir, "tarantool.pid")
    t.assert_equals(fio.path.exists(g.log_path), false)
    t.assert_equals(fio.path.exists(g.pid_path), false)

    local env = {}
    env["TT_PID_FILE"] = g.pid_path
    env["TT_LOG"] = g.log_path
    env["TT_BACKGROUND"] = "true"
    env["TT_WORK_DIR"] = g.work_dir
    g.ph = popen.new(cmd, {
        stderr = popen.opts.PIPE,
        stdin = popen.opts.PIPE,
        stdout = popen.opts.PIPE,
        env = env,
    })
    t.assert_is_not(g.ph, nil)
    t.helpers.retrying({timeout = 2, delay = 0.01}, function(path)
        assert(fio.path.exists(path) == true)
    end, g.log_path)
    g.pid = fio.open(g.pid_path):read()
end)

g.after_test("test_background_mode_env_vars", function(cg)
    os.execute("kill -9 " .. cg.pid)
    fio.unlink(cg.pid_path)
    fio.unlink(cg.log_path)
    os.remove("*.xlog")
    os.remove("*.snap")
end)

g.test_background_mode_env_vars = function(cg)
    t.helpers.retrying({timeout = 2, delay = 0.01}, function()
        assert(check_err_msg(cg.log_path, msg_opt_processing) == true)
    end, cg.log_path, cg.ph)
    check_err_msg(cg.log_path, msg_opt_processing)
end
