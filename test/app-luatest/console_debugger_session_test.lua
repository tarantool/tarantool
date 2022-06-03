local t = require('luatest')
local popen = require('popen')
local tnt = require('tarantool')

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
local dbg_failed_bp = 'command expects argument in format filename:NN or ' ..
                      'filename+NN, where NN should be a positive number.'
local dbg_failed_bpd = 'command expects argument specifying breakpoint or' ..
                       ' * for all breakpoints.'

local function dbg_failed_range(v)
    return ('line number %d is out of allowed range [%d, %d]'):
            format(v, 1, 0x7fffff00)
end

local cmd_aliases = {
    ['b'] = 'b|break|breakpoint|add_break|add_breakpoint',
    ['bd'] = 'bd|bdelete|delete_break|delete_breakpoint',
    ['bl'] = 'bl|blist|list_break|list_breakpoints',
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

local MAX_ALIASES_COUNT = 5

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

local g = t.group('debug', {
    {sequence = 'full scenario'},
    {sequence = 'successful breakpoints additions'},
    {sequence = 'failed breakpoints additions'},
    {sequence = 'successful breakpoints removals'},
    {sequence = 'failed breakpoints removals'},
})

local sequences = {

    -- full, complex sequence
    ['full scenario'] = {
        { ['\t'] = dbg_header }, -- \t is a special value for start
        { ['n'] = dbg_prompt },
        { ['s'] = dbg_prompt },
        { ['n'] = dbg_prompt },
        { ['n'] = dbg_prompt },
        { ['p'] = 'expects argument, but none received' },
        { ['p obj'] = 'obj => {"tzoffset" = "+0300", "hour" = 3}' },
        { ['n'] = dbg_prompt },
        { ['p ymd'] = 'ymd => false' },
        { ['w'] = 'local hms = false' },
        { ['h'] = dbg_prompt },
        { ['t'] = '=> builtin/datetime.lua' },
        { ['u'] = 'debug-target.lua:5 in chunk at' },
        { ['u'] = 'Already at the bottom of the stack.' },
        { ['d'] = 'Inspecting frame: builtin/datetime.lua' },
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
    },

    -- partial sequence with successful breakpoints added
    ['successful breakpoints additions'] = {
        { ['\t'] = dbg_header }, -- \t is a special value for start
        { ['b +11'] = dbg_prompt },
        { ['c'] = 'debug-target.lua:11' },
        { ['n'] = dbg_prompt },
        { ['p S'] = 'S => "1970-01-01T0300+0300"' },
        { ['p T'] = 'T => 1970-01-01T03:00:00+0300' },
        { ['c'] = '' },
    },

    -- partial sequence with failed breakpoint additions
    ['failed breakpoints additions'] = {
        { ['\t'] = dbg_header }, -- \t is a special value for start
        { ['b'] = 'expects argument, but none received' },
        { ['b 11'] = dbg_failed_bp },
        { ['b debug-target.lua'] = dbg_failed_bp },
        { ['b debug-target.lua:'] = dbg_failed_bp },
        { ['b debug-target.lua:-10'] = dbg_failed_bp },
        { ['b debug-target.lua+0'] = dbg_failed_range(0) },
        { ['b +0'] = dbg_failed_range(0) },
        { ['b debug-target.lua+2147483632'] = dbg_failed_range(2147483632) },
        { ['b +21474836480'] = dbg_failed_range(21474836480) },
    },

    -- partial sequence with breakpoints additions and removals
    ['successful breakpoints removals'] = {
        { ['\t'] = dbg_header }, -- \t is a special value for start
        { ['b +7'] = 'debug-target.lua:7' },
        { ['bl'] = 'debug-target.lua:7' },
        { ['b :5'] = 'debug-target.lua:5' },
        { ['bl'] = 'debug-target.lua:5' },
        { ['bd :5'] = 'debug-target.lua:5' },
        { ['bl'] = 'debug-target.lua:7' },
        { ['bd +7'] = 'debug-target.lua:7' },
        { ['b :6'] = 'debug-target.lua:6' },
        { ['bl'] = 'debug-target.lua:6' },
        { ['bd *'] = 'Removed all breakpoints' },
        { ['bl'] = 'No active breakpoints defined' },
    },
    -- partial sequence with failed breakpoints removals
    ['failed breakpoints removals'] = {
        { ['\t'] = dbg_header }, -- \t is a special value for start
        { ['bd'] = 'expects argument, but none received' },
        { ['bd 11'] = dbg_failed_bpd },
        { ['bd debug-target.lua'] = dbg_failed_bpd },
        { ['bd debug-target.lua:'] = dbg_failed_bpd },
        { ['bd debug-target.lua:-10'] = dbg_failed_bpd },
    },
}

local function debug_session(sequence)
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
            t.assert(errout == nil or trim(errout) == '', errout)
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

g.test_debugger = function(cg)
    local scenario_name = cg.params.sequence
    t.assert(scenario_name)
    t.assert(sequences[scenario_name])
    debug_session(sequences[scenario_name])
end
