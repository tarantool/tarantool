local t = require('luatest')
local popen = require('popen')
local tnt = require('tarantool')

local g = t.group()

local function normalize_path(s)
    return s:gsub("^@", ""):gsub("[^/]+$", "")
end

local function unescape(s)
    return s and s:gsub('[\27\155][][()#;?%d]*[A-PRZcf-ntqry=><~]', '') or ''
end

local function trim(s)
    return s:gsub('%s+$', '')
end

local function tarantool_path(arg)
    local index = -2
    -- arg[-1] is guaranteed to be non-null
    while arg[index] do index = index - 1 end
    return arg[index + 1]
end

local TARANTOOL_PATH = tarantool_path(arg)
local path_to_script = normalize_path(debug.getinfo(1, 'S').source)
local debug_target_script = path_to_script .. 'debug-target.lua'

local DEBUGGER = 'luadebug.lua'
local dbg_header = DEBUGGER .. ": Loaded for " .. tnt.version
local dbg_prompt = DEBUGGER .. '>'

local cmd_aliases = {
    ['c'] = 'c|cont|continue',
    ['d'] = 'd|down',
    ['e'] = 'e|eval',
    ['f'] = 'f|finish|step_out',
    ['h'] = 'h|help|?',
    ['l'] = 'l|locals',
    ['n'] = 'n|next|step_over',
    ['p'] = 'p|print',
    ['q'] = 'q|quit',
    ['s'] = 's|st|step|step_into',
    ['t'] = 't|trace|bt',
    ['u'] = 'u|up',
    ['w'] = 'w|where',
}

local MAX_ALIASES_COUNT = 4

local function get_cmd_alias(cmd, number)
    if not cmd_aliases[cmd] then
        return cmd
    end
    local cmds = {}
    for v, _ in cmd_aliases[cmd]:gmatch('[^|]+') do
        table.insert(cmds, v)
    end
    return cmds[number % #cmds + 1]
end

--[[
    Should handle equally well both:
    'n' and 'p obj'
]]
local function get_key_arg_pair(cmd)
    local gen = cmd:gmatch('[^%s]+')
    return gen(), gen()
end

local sequence = {
    { ['\t'] = dbg_header }, -- \t is a special value for start
    { ['n'] = dbg_prompt },
    { ['s'] = dbg_prompt },
    { ['n'] = dbg_prompt },
    { ['n'] = dbg_prompt },
    { ['p'] = 'command expects argument, but none received' },
    { ['p obj'] = 'obj => {"tzoffset" = "+0300", "hour" = 3}' },
    { ['n'] = dbg_prompt },
    { ['p ymd'] = 'ymd => false' },
    { ['w'] = 'local hms = false' },
    { ['h'] = dbg_prompt },
    { ['t'] = 'debug-target.lua:5 in chunk at' },
    { ['u'] = 'Already at the top of the stack.' },
    { ['d'] = 'debug-target.lua:5 in chunk at' },
    { ['u'] = 'Inspecting frame: builtin/datetime.lua' },
    { ['l'] = 'obj => {"tzoffset" = "+0300", "hour" = 3}' },
    { ['f'] = dbg_prompt },
    { ['n'] = dbg_prompt },
    { ['p T'] = 'T => 1970-01-01T03:00:00+0300' },
    { ['n'] = dbg_prompt },
    { [''] = dbg_prompt },
    { [''] = dbg_prompt },
    { [''] = dbg_prompt },
    { ['p S'] = 'S => "1970-01-01T0300+0300"' },
    { ['c'] = '' },
}

g.test_interactive_debugger_session = function()
    local cmd = { TARANTOOL_PATH, debug_target_script }
    --[[
        repeat multiple times to check all command aliases
    ]]
    for i = 1, MAX_ALIASES_COUNT do
        local fh = popen.new(cmd, {
            stdout = popen.opts.PIPE,
            stderr = popen.opts.PIPE,
            stdin = popen.opts.PIPE,
        })
        t.assert_is_not(fh, nil)
        for _, row in pairs(sequence) do
            local cmd, expected = next(row)
            if cmd ~= '\t' then
                if cmd ~= '' then
                    local key, arg = get_key_arg_pair(cmd)
                    arg = arg and (' ' .. arg) or ''
                    cmd = get_cmd_alias(key, i) .. arg .. '\n'
                else -- '' empty command - repeat prior one
                    cmd = cmd .. '\n'
                end
                fh:write(cmd)
            end

            local result
            local clean_cmd = trim(cmd)
            -- there should be empty stderr - check it before stdout
            local errout = fh:read({ timeout = 0.05, stderr = true})
            t.assert(errout == nil or trim(errout) == '')
            repeat
                result = trim(unescape(fh:read({ timeout = 0.5 })))
            until result ~= '' and result ~= clean_cmd
            if expected ~= '' then
                t.assert_str_contains(result, expected, false)
            end
        end
        fh:close()
    end
end
