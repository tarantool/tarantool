local bit = require('bit')
local ffi = require('ffi')
local errno = require('errno')
local fio = require('fio')
local popen = require('popen')
local json = require('json')
local socket = require('socket')
local treegen = require('luatest.treegen')
local t = require('luatest')

local g = t.group()
local RED, YELLOW, RESET = '\27[31m', '\27[33m', '\27[0m'

local function color(message, prefix)
    return prefix and prefix .. message .. RESET or message
end

ffi.cdef([[
    int posix_openpt(int flags);
    int grantpt(int fd);
    int unlockpt(int fd);
    char *ptsname(int fd);
    ssize_t read(int fd, void *buf, size_t count);
    int close(int fd);
]])

g.after_each(function(cg)
    if cg.ph then cg.ph:close() end
    if cg.slave then cg.slave:close() end
    if cg.master then ffi.C.close(cg.master) end
    if cg.sock then
        cg.sock:close()
        cg.sock = nil
    end
end)

local function run_log(cg, code, opts)
    opts = opts or {}

    local fd
    if opts.tty ~= false then
        local flags = bit.bor(fio.c.flag.O_RDWR, fio.c.flag.O_NOCTTY)
        fd = ffi.C.posix_openpt(flags)
        t.assert(fd >= 0)
        cg.master = fd
        t.assert_equals(ffi.C.lbox_socket_nonblock(fd, 1), 1)

        t.assert_equals(ffi.C.grantpt(fd), 0)
        t.assert_equals(ffi.C.unlockpt(fd), 0)
        cg.slave = fio.open(ffi.string(ffi.C.ptsname(fd)), {'O_RDWR'})
        t.assert(cg.slave)

        code = string.format([[
            local ffi = require('ffi')
            ffi.cdef('int dup2(int, int);')
            local tty = %d
            assert(ffi.C.dup2(tty, 1) == 1)
            assert(ffi.C.dup2(tty, 2) == 2)
        ]], cg.slave.fh) .. code
    end

    local args = {arg[-1]}
    if opts.interactive then table.insert(args, '-i') end
    table.insert(args, '-e')
    if not opts.interactive then code = code .. '\nos.exit(0)' end
    table.insert(args, code)

    cg.ph = popen.new(args, {
        stdin = opts.interactive and popen.opts.PIPE or popen.opts.DEVNULL,
        stdout = popen.opts.DEVNULL,
        stderr = popen.opts.PIPE,
        inherit_fds = cg.slave and {cg.slave.fh} or {},
        env = {TERM = opts.term == nil and 'xterm' or opts.term or nil,
               NO_COLOR = opts.no_color or nil},
    })
    if opts.interactive then
        -- Avoid the FreeBSD console hang on EOF by exiting explicitly.
        -- https://github.com/tarantool/tarantool/issues/6231
        assert(cg.ph:write('os.exit(0)\n'))
    end
    local status = assert(cg.ph:wait({timeout = 10}))

    local output = {}
    if fd then
        local buf = ffi.new('char[4096]')
        while true do
            local n = ffi.C.read(fd, buf, 4096)
            if n > 0 then
                table.insert(output, ffi.string(buf, n))
            elseif n == 0 or ffi.errno() == errno.EAGAIN then
                break
            else
                t.assert_equals(ffi.errno(), errno.EINTR)
            end
        end

        cg.slave:close()
        cg.slave = nil
        ffi.C.close(fd)
        cg.master = nil
    else
        while true do
            local chunk = assert(cg.ph:read({stderr = true}))
            if chunk == '' then break end
            table.insert(output, chunk)
        end
    end

    cg.ph:close()
    cg.ph = nil

    output = table.concat(output):gsub('\r', '')
    t.assert_equals(status.exit_code, 0, output)
    return output
end

local plain_logs = [[
    local ffi = require('ffi')
    require('log')
    ffi.C._say(ffi.C.S_ERROR, nil, 0, nil, 'boot')
    ffi.C.say_logger_init(nil, ffi.C.S_DEBUG, 0, 'plain')
    for level = ffi.C.S_FATAL, ffi.C.S_DEBUG do
        ffi.C._say(level, nil, 0, nil, 'message')
    end
    ffi.C.say_set_log_level(ffi.C.S_INFO)
]]

g.test_colors = function(cg)
    for _, tty in ipairs({false, true}) do
        for _, no_color in ipairs({false, '', '0', '1', 'false', 'yes'}) do
            local output = run_log(cg, plain_logs,
                                  {tty = tty, no_color = no_color})
            local enabled = tty and (not no_color or no_color == '')
            local lines = output:rstrip():split('\n')
            t.assert_equals(#lines, 9, output)
            t.assert_equals(lines[1], color('boot', enabled and RED))
            for level = 0, 7 do
                local prefix = enabled and level <= 4 and
                               (level == 4 and YELLOW or RED) or nil
                local line = lines[level + 2]
                t.assert(line:endswith('message' .. (prefix and RESET or '')),
                         line)
                if prefix then
                    t.assert(line:startswith(prefix), line)
                else
                    t.assert_not_str_contains(line, '\27')
                end
            end
        end
    end
end

g.test_term = function(cg)
    local output = run_log(cg, plain_logs, {term = 'dumb'})
    t.assert_not_str_contains(output, '\27')
    output = run_log(cg, plain_logs, {term = false})
    t.assert_str_contains(output, color('boot', RED))
end

g.test_json = function(cg)
    local output = run_log(cg, [[
        local log = require('log')
        log.cfg{format = 'json'}
        log.error('json')
        log.warn('json')
    ]])
    t.assert_not_str_contains(output, '\27')
    local lines = output:rstrip():split('\n')
    t.assert_equals(#lines, 2)
    for _, line in ipairs(lines) do
        t.assert_equals(json.decode(line).message, 'json')
    end
end

g.test_syslog_fatal = function(cg)
    local path = fio.pathjoin(treegen.prepare_directory({}, {}), 'socket')
    cg.sock = socket('AF_UNIX', 'SOCK_DGRAM', 0)
    t.assert(cg.sock:bind('unix/', path))
    local output = run_log(cg, string.format([[
        local ffi = require('ffi')
        require('log').cfg{log = 'syslog:server=unix:%s'}
        ffi.C._say(ffi.C.S_FATAL, nil, 0, nil, 'fatal')
    ]], path))
    local _, count = output:gsub('F> fatal', '')
    t.assert_equals(count, 1, output)
    t.assert_str_contains(output, RED)
    t.assert_str_contains(output, 'fatal' .. RESET)
end

g.test_stderr_redirect = function(cg)
    local path = fio.pathjoin(treegen.prepare_directory({}, {}), 'log')
    local output = run_log(cg, string.format([[
        local file = require('fio').open(%q, {'O_RDWR', 'O_CREAT'})
        local log = require('log')
        log.error('before')
        assert(ffi.C.dup2(file.fh, 2) == 2)
        log.error('file')
        assert(ffi.C.dup2(tty, 2) == 2)
        log.error('after')
        file:seek(0)
        io.stdout:write(file:read())
        file:close()
    ]], path))
    t.assert_equals(output, color('before', RED) .. '\n' ..
                    color('after', RED) .. '\nfile\n')
end

g.test_banner = function(cg)
    for _, level in ipairs({2, 3}) do
        local output = run_log(cg, string.format([[
            require('log').cfg{level = %d}
            require('log').error('color check')
        ]], level), {no_color = '', interactive = true})
        t.assert_str_contains(output, RED)
        local banner = output:match('([^\r\n]*Tarantool.-interactive help)')
        if level == 2 then
            t.assert_equals(banner, nil)
        else
            t.assert(banner)
            t.assert_not_str_contains(banner, '\27')
            t.assert_str_contains(banner, 'C>')
        end
    end
end
