local M = {}

local USAGE_MSG = [[
usage: tarantool -t<cmd>
Available tools are:
  -m [--leak-only] input  Memprof profile data parser.
  -s input                Sysprof profile data parser.
]]

local function execute_parser(profile_parser, arg)
    local st, parser = pcall(require, profile_parser)
    if not st then
        error('unknown luaJIT command or tools not installed')
    end
    parser(arg)
end

M.dotoolcmd = function(...)
    local arg = {...}
    local tool_name = string.sub(arg[1], 3, #arg[1])
    table.remove(arg, 1)
    if tool_name == 'm' then
        execute_parser('memprof', arg)
    elseif tool_name == 's' then
        execute_parser('sysprof', arg)
    else
        error(USAGE_MSG)
    end
end

return M
