local server = require('luatest.server')
local popen = require('popen')
local clock = require('clock')

local t = require('luatest')
local g = t.group()

g.after_each = function()
    if g.server ~= nil then
       g.server:stop()
   end
end

local TARANTOOL_PATH = arg[-1]

local function popen_run(command)
    local cmd = TARANTOOL_PATH .. ' -i 2>&1'
    local ph = popen.new({cmd}, {
        shell = true,
        setsid = true,
        group_signal = true,
        stdout = popen.opts.PIPE,
        stderr = popen.opts.DEVNULL,
        stdin = popen.opts.PIPE,
    })
    t.assert(ph, 'process is not up')

    ph:write(command)

    local output = ''
    local time_quota = 10.0
    local start_time = clock.monotonic()
    while clock.monotonic() - start_time < time_quota do
        local chunk = ph:read({timeout = 1.0})
        if chunk == '' or chunk == nil then
            -- EOF or error
            break
        end
        output = output .. chunk
    end

    ph:close()
    return output
end

g.test_json_table_curly_bracket = function()
    local env = {["TT_METRICS"] = '{"labels":{"alias":"gh_8051"},' ..
                                  '"include":"all","exclude":["vinyl"]}'}

    g.server = server:new{alias='json_table_curly_bracket', env=env}
    g.server:start()

    t.assert_equals(g.server:get_box_cfg().metrics.labels.alias, 'gh_8051')
end

g.test_json_table_square_bracket = function()
    local res = popen_run([=[
        os.setenv('TT_LISTEN', '["localhost:0"]')
        box.cfg{}
        box.cfg.listen
    ]=])

    t.assert_str_contains(res, "- ['localhost:0']")
end

g.test_plain_table = function()
    local env = {["TT_LOG_MODULES"] = 'aaa=info,bbb=error'}

    g.server = server:new{alias='plain_table', env=env}
    g.server:start()

    t.assert_equals(g.server:get_box_cfg().log_modules,
                    {['aaa'] = 'info', ['bbb'] = 'error'})
end

g.test_format_error = function()
    local res = popen_run([[
        os.setenv('TT_LOG_MODULES', 'aaa=info,bbb')
        box.cfg{}
    ]])

    t.assert_str_contains(res, "in `key=value` or `value` format'")
end

g.test_format_error_empty_key = function()
    local res = popen_run([[
        os.setenv('TT_LOG_MODULES', 'aaa=info,=error')
        box.cfg{}
    ]])

    t.assert_str_contains(res, "`key` must not be empty'")
end
