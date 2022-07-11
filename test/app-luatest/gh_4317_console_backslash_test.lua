local t = require('luatest')
local g = t.group()

local result_str =  [=[tarantool> \set continuation on
---
- true
...

tarantool> local a = 0\
         > for i = 1, 10 do
         > a = a + i
         > end\
         > print(a)
55
---
...

tarantool> print([[
         > aaa \
         > bbb
         > ]])
aaa \
bbb

---
...

tarantool> ]=]

local TARANTOOL_PATH = arg[-1]

g.test_using_backslash_on_local_console = function()
    local tarantool_command = [[\\set continuation on\nlocal a = 0\\\n]] ..
                              [[for i = 1, 10 do\na = a + i\nend\\\n]] ..
                              [[print(a)\n]] ..
                              [[print([[\n]] ..
                              [[aaa \\\n]] ..
                              [[bbb\n]] ..
                              [=[]])]=]

    local cmd = "printf '%s' | %s -i 2>/dev/null"
    cmd = (cmd):format(tarantool_command, TARANTOOL_PATH)
    local fh = io.popen(cmd, 'r')

    -- XXX: Readline on CentOS 7 produces \e[?1034h escape sequence before
    -- tarantool> prompt, remove it.
    local result = fh:read('*a'):gsub('\x1b%[%?1034h', '')

    fh:close()
    t.assert_equals(result, result_str)
end

g.test_using_backslash_on_remote_console = function()
    local console = require('console')
    local socket = require('socket')
    local log = require('log')

    -- Suppress console log messages.
    log.level(4)
    local CONSOLE_SOCKET = '/tmp/tarantool-test-gh-4317.sock'
    local EOL = '\n...\n'
    console.listen(CONSOLE_SOCKET)
    socket.tcp_connect('unix/', CONSOLE_SOCKET)
    local tarantool_command = '\\set continuation on\nlocal a = 0 \\\n' ..
                              'for i = 1, 10 do\\\n a = a + i \\\n end \\\n' ..
                              'return a'

    local client = socket.tcp_connect('unix/', CONSOLE_SOCKET)
    client:write(('%s\n'):format(tarantool_command))
    -- Read 'true' after '\set continuation on' and then read result.
    client:read(EOL)
    local result = client:read(EOL)
    t.assert_str_contains(result, '- 55')
    t.assert_not_str_contains(result, 'error')
    client:close()
    os.remove(CONSOLE_SOCKET)
end
