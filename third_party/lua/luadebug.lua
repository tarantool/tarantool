--[[
	Copyright (c) 2020 Scott Lembcke and Howling Moon Software

	Permission is hereby granted, free of charge, to any person obtaining a copy
	of this software and associated documentation files (the "Software"), to deal
	in the Software without restriction, including without limitation the rights
	to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
	copies of the Software, and to permit persons to whom the Software is
	furnished to do so, subject to the following conditions:

	The above copyright notice and this permission notice shall be included in
	all copies or substantial portions of the Software.

	THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
	IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
	FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
	AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
	LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
	OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
	SOFTWARE.

	TODO:
	* Print short function arguments as part of stack location.
	* Properly handle being reentrant due to coroutines.
]]

local dbg

local HOME_DIR = os.getenv('HOME')
local DEBUGGER = 'luadebug'
local HISTORYFILE = HOME_DIR ~= nil and HOME_DIR .. '/' .. '.tdbg_history' or nil
-- Use ANSI color codes in the prompt by default.
local COLOR_GRAY = ""
local COLOR_RED = ""
local COLOR_GREEN
local COLOR_BLUE = ""
local COLOR_YELLOW = ""
local COLOR_RESET = ""
local CARET_SYM = "=>"
local CARET = " " .. CARET_SYM .. " "
local BREAK_SYM = "●"

local LJ_MAX_LINE = 0x7fffff00 -- Max. source code line number.

local function pretty(obj, max_depth)
    if max_depth == nil then
        max_depth = dbg.cfg.pretty_depth
    end

    -- Returns true if a table has a __tostring metamethod.
    local function coerceable(tbl)
        local meta = getmetatable(tbl)
        return (meta and meta.__tostring)
    end

    local function recurse(obj, depth)
        if type(obj) == "string" then
            -- Dump the string so that escape sequences are printed.
            return string.format("%q", obj)
        elseif type(obj) == "table" and depth < max_depth and not coerceable(obj) then
            local str = "{"

            for k, v in pairs(obj) do
                local pair = pretty(k, 0) .. " = " .. recurse(v, depth + 1)
                str = str .. (str == "{" and pair or ", " .. pair)
            end

            return str .. "}"
        else
            -- tostring() can fail if there is an error in a __tostring metamethod.
            local success, value = pcall(function() return tostring(obj) end)
            return (success and value or "<!!error in __tostring metamethod!!>")
        end
    end

    return recurse(obj, 0)
end

local function get_stack_length(offset)
    local index = offset + 1
    while true do
        if not debug.getinfo(index) then
            break
        end
        index = index + 1
    end
    return index - offset - 1
end

-- The stack level that cmd_* functions use to access locals or info
-- The structure of the code very carefully ensures this.
local CMD_STACK_LEVEL = 6

-- Location of the top of the stack outside of the debugger.
-- Adjusted by some debugger entrypoints.
local stack_top = 0

-- The current stack frame index.
-- Changed using the up/down commands
local stack_inspect_offset = 0

-- Tarantool console compatible readline support.
local function dbg_readline(prompt)
    local console = require('console.lib')
    local line = console.readline({
        prompt = prompt,
        completion = nil,
    })
    if not line then
        return nil
    end
    console.add_history(line)
    if HISTORYFILE then
        console.save_history(HISTORYFILE)
    end
    return line
end

-- Default dbg.write function
local function dbg_write(str)
    io.write(str)
end

local function dbg_writeln(str, ...)
    if select("#", ...) == 0 then
        dbg.write((str or "<NULL>") .. "\n")
    else
        dbg.write(string.format(str .. "\n", ...))
    end
end

-- colored text output wrappers
local function color_blue(text)
    return COLOR_BLUE .. text .. COLOR_RESET
end

local function color_yellow(text)
    return COLOR_YELLOW .. text .. COLOR_RESET
end

local function color_red(text)
    return COLOR_RED .. text .. COLOR_RESET
end

local function color_grey(text)
    return COLOR_GRAY .. text .. COLOR_RESET
end

-- report error highlighted in color
local function dbg_write_error(str, ...)
    dbg_writeln(color_red('Error: ') .. str, ...)
end

-- report non-fatal warnings highlighted in color
local function dbg_write_warn(str, ...)
    dbg_writeln(color_yellow('Warning: ') .. str, ...)
end

local function q(text)
    return "'" .. text .. "'"
end

local function format_loc(file, line)
    return color_blue(file) .. ":" .. color_yellow(line)
end

local function format_stack_frame_info(info)
    local filename = info.source:match("@(.*)")
    local source = filename and dbg.shorten_path(filename) or info.short_src
    local namewhat = (info.namewhat == "" and "chunk at" or info.namewhat)
    local name = info.name and q(color_blue(info.name)) or
                 format_loc(source, info.linedefined)
    return format_loc(source, info.currentline) .. " in " .. namewhat .. " " .. name
end

local fio = require 'fio'
local cwd = fio.cwd()

local suffix_arrays = {} -- memoize file x tree resuls
local normalized_files = {} -- memoize normalization results

--[[
    Normalize `debug.getinfo().source` names:
    - remove leading '@';
    - remove trailing new lines (if any);
    - if there is leading '.' or '..' then calculate full path
      using current directory.
]]
local function normalize_file(file, resolve_dots)
    local srcfile = file
    if not file then
        return ''
    end
    if normalized_files[file] then
        return normalized_files[file]
    end
    file = file:gsub('^@', ''):gsub('\n', '')
    if resolve_dots and file:sub(1,1) == '.' then
        file = fio.abspath(fio.pathjoin(cwd, file))
    end
    normalized_files[srcfile] = file
    return file
end

-- This function is on a hot path of a debugger hook.
-- Try very hard to avoid wasting of CPU cycles, so reuse
-- previously calculated results via memoization
local function build_suffix_array(file, resolve_dots)
    local decorated_name = file
    if suffix_arrays[decorated_name] ~= nil then
        return suffix_arrays[decorated_name]
    end

    local suffixes = {}
    file = normalize_file(file, resolve_dots)
    --[[
        we would very much prefer to use simple:
           for v in file:gmatch('[^/]+') do
        loop here, but string.gmatch is very slow
        and that we actually need here are simple strchr's.
    ]]
    local i = 0
    local j
    local v
    repeat
        j = file:find('/', i, true)
        if j ~= nil then
            v = file:sub(i, j - 1)
            if v ~= '.' then -- just skip '.' for current dir
                if v == '..' then
                    table.remove(suffixes)
                else
                    table.insert(suffixes, 1, v)
                end
            end
            i = j + 1
        end
    until j == nil
    -- don't forget to process the trailing segment
    v = file:sub(i, #file)
    if v ~= '.' then -- just skip '.' for current dir
        if v == '..' then
            table.remove(suffixes)
        else
            table.insert(suffixes, 1, v)
        end
    end

    suffix_arrays[decorated_name] = suffixes
    return suffixes
end

-- Merge suffix array A to the tree T:
--   Given input array ['E.lua','C','B'] which is suffix array
--   corresponding to the input path '@B/./C/E.lua',
--   we build tree of a form:
--     T['E.lua']['C']['B'] = {}
local function append_suffix_array(T, A)
    assert(type(T) == 'table')
    assert(type(A) == 'table')
    if #A < 1 then
        return
    end
    local C = T -- advance current node, starting from root
    local P -- prior node
    for i = 1, #A do
        local v = A[i]
        if not C[v] then
            C[v] = {}
            C[v]['$ref'] = 1 --reference counter
        else
            C[v]['$ref'] = C[v]['$ref'] + 1
        end
        P = C
        C = C[v]
    end
    local last = A[#A]
    P[last]['$f'] = true
end

-- lookup into suffix tree T given constructed suffix array S
local function lookup_suffix_array(T, S)
    if #S < 1 then
        return false
    end
    -- we need to make sure that at least once
    -- we have matched node inside of loop
    -- so bail out immediately if there is no any single
    -- match
    if T[S[1]] == nil then
        return false
    end

    local C = T
    local P -- last accessed node
    local v
    for i = 1, #S do
        v = S[i]
        if C[v] == nil then
            return not not P[S[i - 1]]['$f']
        end
        P = C
        C = C[v]
    end
    return not not P[v]['$f']
end

--[[
    Given suffix tree T try to remove suffix array A
    from the tree. Leafs and intermediate nodes will be
    cleaned up only once their reference counter '$ref'
    will reach 1.
]]
local function remove_suffix_array(T, A)
    local C = T
    local mem_walk = {}
    -- walks down tree, remembering pointers for inner directories
    for i = 1, #A do
        mem_walk[i] = C
        local v = A[i]
        if C[v] == nil then
            return false
        end
        C = C[v]
    end

    -- now walk in revert order cleaning subnodes,
    -- starting from deepest reachable leaf
    for i = #A, 1, -1 do
        C = mem_walk[i]
        local v = A[i]
        assert(C[v] ~= nil)
        assert(C[v]['$ref'] >= 1)
        C[v]['$ref'] = C[v]['$ref'] - 1
        if C[v]['$ref'] <= 1 then
            mem_walk[i] = nil
            C[v] = nil
        end
    end
end

local myself = normalize_file(debug.getinfo(1,'S').source)
local breakpoints = {}

local function has_breakpoint(file, line)
    local T = breakpoints[line]
    if T == nil then
        return false
    end
    local suffixes = build_suffix_array(file)
    return lookup_suffix_array(T, suffixes)
end

local function has_active_breakpoints()
    local key = next(breakpoints)
    return key ~= nil
end

local function add_breakpoint(file, line)
    local suffixes = build_suffix_array(file, true)
    local normfile = normalize_file(file, true)
    -- save direct hash (for faster lookup): line -> file
    if not breakpoints[line] then
        breakpoints[line] = {}
    end
    append_suffix_array(breakpoints[line], suffixes)
    -- save reversed hash information: file -> [lines]
    if not breakpoints[normfile] then
        breakpoints[normfile] = {}
    end
    breakpoints[normfile][line] = true
end

local function remove_breakpoint(file, line)
    local normfile = normalize_file(file, true)
    if breakpoints[line] == nil then
        return false
    end
    local suffixes = build_suffix_array(file, true)
    remove_suffix_array(breakpoints[line], suffixes)
    breakpoints[normfile][line] = nil
end

local repl

-- Return false for stack frames without source,
-- which includes C frames, Lua bytecode, and `loadstring` functions
local function frame_has_line(info) return info.currentline >= 0 end

local dbg_hook_ofs = 2

local function hook_factory(level)
    local stop_level = level or -1
    return function(reason)
        return function(event, _)
            -- Skip events that don't have line information.
            local info = debug.getinfo(2, "Sl")
            local normfile = normalize_file(info.source)

            if not frame_has_line(info) or normfile == myself then
                return
            end

            -- [Correction logics borrowed from mobdebug.lua]
            -- This is needed to check if the stack got shorter or longer.
            -- Unfortunately counting call/return calls is not reliable.
            -- The discrepancy may happen when "pcall(load, '')" call is made
            -- or when "error()" is called in a function.
            -- Start from one level higher just in case we need to grow the
            -- stack. This may happen after coroutine.resume call to a function
            -- that doesn't have any other instructions to execute. It triggers
            -- three returns: "return, tail return, return", which needs to be
            -- accounted for.
            local offset = get_stack_length(dbg_hook_ofs)

            if event == "line" then
                local matched = offset <= stop_level or
                                has_breakpoint(normfile, info.currentline)
                if matched then
                    repl(reason)
                end
            end
        end
    end
end

-- For the breakpoint hook, we will bail out of a function
-- for a majority of a calls (say in 99.999% of a cases).
-- So it's very important to spend as little time as possible
-- here: quick return if irrelevant, allocate the least possible
-- memory, memoize calculated values - as for neighborhood lines it will
-- be mostly the same
local function hook_check_bps()
    return function(event, _)
        -- Skip events that don't have line information.
        local info = debug.getinfo(2, "Sl")
        if not frame_has_line(info) then
            return
        end
        if event == 'line' and has_breakpoint(info.source, info.currentline) then
            repl('hit')
        end
    end
end

-- Create a table of all the locally accessible variables.
-- Globals are not included when running the locals command, but are when running the print command.
local function local_bindings(offset, include_globals)
    local level = offset + stack_inspect_offset + CMD_STACK_LEVEL
    local func = debug.getinfo(level, "f").func
    local bindings = {}

    -- Retrieve the upvalues
    do  local i = 1;
        while true do
            local name, value = debug.getupvalue(func, i)
            if not name then break end
            bindings[name] = value
            i = i + 1
        end
    end

    -- Retrieve the locals (overwriting any upvalues)
    do  local i = 1;
        while true do
            local name, value = debug.getlocal(level, i)
            if not name then break end
            bindings[name] = value
            i = i + 1
        end
    end

    -- Retrieve the varargs (works in Lua 5.2 and LuaJIT)
    local varargs = {}
    do  local i = 1;
        while true do
            local name, value = debug.getlocal(level, -i)
            if not name then break end
            varargs[i] = value
            i = i + 1
        end
    end
    if #varargs > 0 then bindings["..."] = varargs end

    if include_globals then
        return setmetatable(bindings, { __index = getfenv(func) or _G })
    else
        return bindings
    end
end

-- Used as a __newindex metamethod to modify variables in cmd_eval().
local function mutate_bindings(_, name, value)
    local FUNC_STACK_OFFSET = 3 -- Stack depth of this function.
    local level = stack_inspect_offset + FUNC_STACK_OFFSET + CMD_STACK_LEVEL

    -- Set a local.
    do  local i = 1;
        repeat
            local var = debug.getlocal(level, i)
            if name == var then
                dbg_writeln(color_yellow(DEBUGGER) .. CARET ..
                            "Set local variable " .. color_blue(name))
                return debug.setlocal(level, i, value)
            end
            i = i + 1
        until var == nil
    end

    -- Set an upvalue.
    local func = debug.getinfo(level).func
    do  local i = 1;
        repeat
            local var = debug.getupvalue(func, i)
            if name == var then
                dbg_writeln(color_yellow(DEBUGGER) ..
                            "Set upvalue " .. color_blue(name))
                return debug.setupvalue(func, i, value)
            end
            i = i + 1
        until var == nil
    end

    -- Set a global.
    dbg_writeln(color_yellow(DEBUGGER) ..
                "Set global variable " .. color_blue(name))
    _G[name] = value
end

-- Compile an expression with the given variable bindings.
local function compile_chunk(block, env)
    local source = DEBUGGER .. " REPL"
    local chunk = loadstring(block, source)
    if chunk then
        setfenv(chunk, env)
    else
        dbg_write_error("Could not compile block: %s", q(block))
    end
    return chunk
end

local SOURCE_CACHE = {}
local tnt = nil

local function code_listing(source, currentline, file, context_lines)
    local normfile = normalize_file(file)
    if source and source[currentline] then
        for i = currentline - context_lines,
                currentline + context_lines do
            local break_at = has_breakpoint(normfile, i)
            local tab_or_caret = (i == currentline and CARET_SYM or "  ")
                                 .. (break_at and BREAK_SYM or " ")
            local line = source[i]
            if line then
                dbg_writeln(color_grey("% 4d") .. tab_or_caret .. "%s", i, line)
            end
        end
        return true
    else
        dbg_write_warn("No source code is available for %s:%d",
                       color_blue(normfile), currentline or 0)
        return true
    end
end

local function where(info, context_lines)
    local filesource = info.source
    local source = SOURCE_CACHE[info.source]
    if not source then
        source = {}
        -- Tarantool builtin module
        if filesource:match("@builtin/.*.lua") then
            pcall(function()
                local lua_code = tnt.debug.getsources(filesource)

                for line in string.gmatch(lua_code, "([^\n]*)\n?") do
                    table.insert(source, line)
                end
            end)
        else
            -- external module - load file
            local filename = filesource:match("@(.*)") or filesource
            if filename then
                pcall(function()
                    for line in io.lines(filename) do
                        table.insert(source, line)
                    end
                end)
            elseif filesource then
                for line in info.source:gmatch("(.-)\n") do
                    table.insert(filesource, line)
                end
            end
        end
        SOURCE_CACHE[info.source] = source
    end

    return code_listing(source, info.currentline, info.source, context_lines)
end

-- Status flag to avoid redundant code listing to be shown.
-- `up`, `down`, `where` commands show their own code context
-- so do not show anything after them.
local listing_shown = false

-- Display automatically `where` context if configured so.
local function auto_listing(info)
    if tonumber(dbg.cfg.auto_where) then
        return where(info, dbg.cfg.auto_where)
    end
end

-- Wee version differences
local unpack = unpack or table.unpack
local pack = function(...) return { n = select("#", ...), ... } end

local current_stack_level = function() return get_stack_length(stack_top) end

local function cmd_step()
    return true, hook_factory(math.huge)
end

local function cmd_next()
    return true, hook_factory(current_stack_level() - CMD_STACK_LEVEL)
end

local function cmd_finish()
    return true, hook_factory(current_stack_level() - CMD_STACK_LEVEL - 1)
end

-- If there is no any single defined breakpoint yet, then
-- do not stop in debugger hooks
local function cmd_continue()
    if not has_active_breakpoints() then
        return true
    end
    return true, hook_check_bps
end

local function cmd_print(expr)
    local env = local_bindings(1, true)
    local chunk = compile_chunk("return " .. expr, env)
    if chunk == nil then return false end

    -- Call the chunk and collect the results.
    local results = pack(pcall(chunk, unpack(rawget(env, "...") or {})))

    -- The first result is the pcall error.
    if not results[1] then
        dbg_write_error(results[2])
    else
        local output = ""
        for i = 2, results.n do
            output = output .. (i ~= 2 and ", " or "") .. pretty(results[i])
        end

        if output == "" then output = "<no result>" end
        dbg_writeln(color_blue(expr) .. CARET .. output)
    end

    return false
end

local function cmd_eval(code)
    local env = local_bindings(1, true)
    local mutable_env = setmetatable({}, {
        __index = env,
        __newindex = mutate_bindings,
    })

    local chunk = compile_chunk(code, mutable_env)
    if chunk == nil then return false end

    -- Call the chunk and collect the results.
    local success, err = pcall(chunk, unpack(rawget(env, "...") or {}))
    if not success then
        dbg_write_error(tostring(err))
    end

    return false
end

--[[
    Parse source location syntax. One of a kind:
        luadebug.lua:100
        luadebug.lua+100
        :100
        +100
--]]
local function parse_bp(expr)
    assert(expr)
    local from, to, file, line = expr:find('^(.-)%s*[+:](%d+)%s*$')
    if not from then return nil end
    assert(to == #expr)
    return file, tonumber(line)
end

local function _bp_format_expected()
    dbg_write_error("command expects argument in format filename:NN or " ..
                    "filename+NN, where NN should be a positive number.")
end

-- check range to fit in boundaries {from, to}
local function check_line_max(v, from, to)
    if type(v) ~= 'number' then
        dbg_write_error("numeric value expected as line number, " ..
                        "but received %s", type(v))
        return false
    end
    if v >= from and v <= to then
        return true
    end
    dbg_write_error("line number %d is out of allowed range [%d, %d]",
                    v, from, to)
    return false
end


local function cmd_add_breakpoint(bps)
    assert(bps)
    local fullfile, line = parse_bp(bps)
    if not fullfile then
        _bp_format_expected()
        return false
    end
    if not check_line_max(line, 1, LJ_MAX_LINE) then
        return false
    end
    local info = debug.getinfo(stack_inspect_offset + CMD_STACK_LEVEL, "S")
    local debuggee = info.source
    local file = #fullfile > 0 and fullfile or debuggee
    local normfile = normalize_file(file)

    add_breakpoint(normfile, line)
    dbg_writeln("Added breakpoint %s:%d.", color_blue(normfile), line)
    listing_shown = auto_listing({source = file, currentline = line})
    return false
end

local function _bpd_format_expected()
    dbg_write_error(" command expects argument specifying breakpoint or" ..
                    " * for all breakpoints.")
end

local function cmd_remove_breakpoint(bps)
    assert(bps)
    if bps == '*' then
        breakpoints = {}
        dbg_writeln("Removed all breakpoints.")
        return false
    end

    local fullfile, line = parse_bp(bps)
    if not fullfile then
        _bpd_format_expected()
        return false
    end
    if not check_line_max(line, 1, LJ_MAX_LINE) then
        return false
    end
    local info = debug.getinfo(stack_inspect_offset + CMD_STACK_LEVEL, "S")
    local debuggee = info.source
    local file = #fullfile > 0 and fullfile or debuggee
    local normfile = normalize_file(file)
    local removed = remove_breakpoint(normfile, line)
    if removed then
        dbg_writeln("Removed breakpoint %s:%d.", color_blue(normfile), line)
    else
        dbg_write_warn("No breakpoint defined in %s:%d.",
                       color_blue(normfile), line)
    end
    return false
end

local function cmd_list_breakpoints()
    if has_active_breakpoints() then
        dbg_writeln("List of active breakpoints:")
        -- we keep both lines->file and file->line in the same
        -- breakpoints[] array, thus we should filter out strings
        for file, breaks in pairs(breakpoints) do
            if type(file) == 'string' then
                local sorted = {}
                for line, _ in pairs(breaks) do
                    table.insert(sorted, line)
                end
                table.sort(sorted)
                for _, line in pairs(sorted) do
                    dbg_writeln("\t%s:%d", file, line)
                end
            end
        end
    else
        dbg_write_warn("No active breakpoints defined.")
    end
    return false
end

--[[
    Go up the stack. It advances from the callee frame to the caller.
]]
local function cmd_up()
    local offset = stack_inspect_offset
    local info

    repeat -- Find the next frame with a file.
        offset = offset + 1
        info = debug.getinfo(offset + CMD_STACK_LEVEL, "Snl")
    until not info or frame_has_line(info)

    if info then
        stack_inspect_offset = offset
        dbg_writeln("Inspecting frame: " .. format_stack_frame_info(info))
        auto_listing(info)
    else
        dbg_write_warn("Already at the bottom of the stack.")
    end
    listing_shown = true

    return false
end

--[[
    Go down the stack. It advances from the caller frame to the callee.
]]
local function cmd_down()
    local offset = stack_inspect_offset
    local info

    repeat -- Find the next frame with a file.
        offset = offset - 1
        if offset < stack_top then info = nil; break end
        info = debug.getinfo(offset + CMD_STACK_LEVEL)
    until frame_has_line(info)

    if info then
        stack_inspect_offset = offset
        dbg_writeln("Inspecting frame: " .. format_stack_frame_info(info))
        auto_listing(info)
    else
        dbg_write_warn("Already at the top of the stack.")
    end
    listing_shown = true

    return false
end

local function cmd_where(context_lines)
    local info = debug.getinfo(stack_inspect_offset + CMD_STACK_LEVEL, "Sl")
    if info then
        where(info, tonumber(context_lines) or 5)
    end
    listing_shown = true
    return false
end

local function cmd_trace()
    dbg_writeln("Inspecting frame %d", stack_inspect_offset - stack_top)
    local i = 0;
    while true do
        local info = debug.getinfo(stack_top + CMD_STACK_LEVEL + i, "Snl")
        if not info then break end

        local is_current_frame = (i + stack_top == stack_inspect_offset)
        local tab_or_caret = (is_current_frame and CARET or "    ")
        dbg_writeln(color_grey("% 4d") .. tab_or_caret .. "%s",
                    i, format_stack_frame_info(info))
        i = i + 1
    end

    return false
end

local function cmd_locals()
    local bindings = local_bindings(1, false)

    -- Get all the variable binding names and sort them
    local keys = {}
    for k, _ in pairs(bindings) do table.insert(keys, k) end
    table.sort(keys)

    for _, k in ipairs(keys) do
        local v = bindings[k]

        -- Skip the debugger object itself, "(*internal)" values, and Lua 5.2's _ENV object.
        if not rawequal(v, dbg) and k ~= "_ENV" and not k:match("%(.*%)") then
            dbg_writeln("  " .. color_blue(k) .. CARET .. pretty(v))
        end
    end

    return false
end

local gen_commands

local function cmd_help()
    for _, v in ipairs(gen_commands) do
        local map = gen_commands[v]
        if #map.aliases > 0 then
            local fun = require 'fun'
            local txt = ''
            fun.each(function(x) txt = txt .. ', ' .. color_yellow(x) end,
                     map.aliases)
            dbg.writeln(color_blue(v) .. ', ' .. string.sub(txt, 3, #txt) ..
                        ' ' .. (map.arg or ''))
        else
            dbg.writeln(color_blue(v) .. ' ' .. (map.arg or ''));
        end
        dbg.writeln(color_grey('    -- ' .. map.help))
    end
    dbg.writeln('')

    return false
end

local function cmd_quit()
    dbg.exit(0)
    return true
end

local commands_help = {
    {'b.reak.point $location|add_break.point', 'set new breakpoints at module.lua+num', cmd_add_breakpoint},
    {'bd.elete $location|delete_break.point', 'delete breakpoints',  cmd_remove_breakpoint},
    {'bl.ist|list_break.points', 'list breakpoints', cmd_list_breakpoints},
    {'c.ont.inue', 'continue execution', cmd_continue},
    {'d.own', 'move down the stack by one frame',  cmd_down},
    {'e.val $expression', 'execute the statement',  cmd_eval},
    {'f.inish|step_out', 'step forward until exiting the current function',  cmd_finish},
    {'h.elp|?', 'print this help message',  cmd_help},
    {'l.ocals', 'print the function arguments, locals and upvalues',  cmd_locals},
    {'n.ext|step_over', 'step forward by one line (skipping over functions)',  cmd_next},
    {'p.rint $expression', 'execute the expression and print the result',  cmd_print},
    {'q.uit', 'exit debugger', cmd_quit},
    {'s.t.ep|step_into', 'step forward by one line (into functions)', cmd_step},
    {'t.race|bt', 'print the stack trace',  cmd_trace},
    {'u.p', 'move up the stack by one frame',  cmd_up},
    {'w.here $linecount', 'print source code around the current line', cmd_where},
}

local function build_commands_map(commands)
    local gen_commands = {}

    for _, cmds in ipairs(commands) do
        local c, h, f = unpack(cmds)
        local first = true
        local main_cmd
        local pattern = '^[^%s]+%s+([^%s]+)'
        --[[
            "expected argument" is treated as a global attribute,
            active for all command's aliases.
        ]]
        local arg_exp = false

        for subcmds in c:gmatch('[^|]+') do
            local arg = subcmds:match(pattern)
            subcmds = subcmds:match('^([^%s]+)')
            local cmd = ''
            local gen = subcmds:gmatch('[^.]+')
            local prefix = gen()
            local suffix = ''
            local segment = prefix

            -- remember the first segment (main shortcut for command)
            if first then
                main_cmd = prefix
                arg_exp = arg
            end

            repeat
                cmd = cmd .. segment
                gen_commands[cmd] = {
                    help = h,
                    handler = f,
                    first = first,
                    suffix = suffix,
                    aliases = {},
                    arg = arg_exp
                }
                if first then
                    table.insert(gen_commands, main_cmd)
                else
                    assert(#main_cmd > 0)
                    table.insert(gen_commands[main_cmd].aliases, cmd)
                end
                first = false
                segment = gen()
                suffix = suffix .. (segment or '')
            until not segment
        end
    end
    return gen_commands
end

gen_commands = build_commands_map(commands_help)

local last_cmd = false

-- Recognize a command, then return command handler,
-- 1st argument passed, and flag what argument is expected.
local function match_command(line)
    local gen = line:gmatch('[^%s]+')
    local cmd = gen()
    local arg1st = gen()
    if not gen_commands[cmd] then
        return nil
    else
        return gen_commands[cmd].handler, arg1st, gen_commands[cmd].arg
    end
end

-- Run a command line
-- Returns true if the REPL should exit and the hook function factory
local function run_command(line)
    -- GDB/LLDB exit on ctrl-d
    if line == nil then dbg.exit(1); return true end

    -- Re-execute the last command if you press return.
    if line == "" then line = last_cmd or "h" end

    local handler, command_arg, arg_expected = match_command(line)
    if handler then
        if arg_expected and command_arg == nil then
            dbg_write_error("command '%s' expects argument, but none received.\n" ..
                            "Type 'h' and press return for a command list.",
                            line)
            return false
        else
            last_cmd = line
            -- unpack({...}) prevents tail call elimination so the stack frame indices are predictable.
            return unpack({ handler(command_arg) })
        end
    elseif dbg.cfg.auto_eval then
        return unpack({ cmd_eval(line) })
    else
        dbg_write_error("command '%s' is not recognized.\n" ..
                        "Type 'h' and press return for a command list.",
                        line)
        return false
    end
end

local started = false

local function motto()
    -- Detect Tarantool version.
    if not tnt then
        tnt = require('tarantool')
        assert(tnt ~= nil)
    end
    dbg_writeln(color_yellow(DEBUGGER .. ": ") .. "Loaded for " .. tnt.version)
    jit.off()
    jit.flush()
end

-- lazily perform repl initialization
local function start_repl()
    if started then
        return
    end
    motto()
    if HISTORYFILE then
        require('console.lib').load_history(HISTORYFILE)
    end
    started = true
end

repl = function(reason)
    start_repl()
    -- Skip frames without source info.
    while not frame_has_line(debug.getinfo(stack_inspect_offset +
                                           CMD_STACK_LEVEL - 3, "l")) do
        stack_inspect_offset = stack_inspect_offset + 1
    end

    local info = debug.getinfo(stack_inspect_offset + CMD_STACK_LEVEL - 3,
                               "Snl")
    reason = reason and (color_yellow("break via ") .. color_red(reason) ..
             CARET) or ""
    dbg_writeln(reason .. format_stack_frame_info(info))

    repeat
        -- Do not current context if prior command showed their own.
        if not listing_shown then
            auto_listing(info)
        end
        -- Command could show their own context with listing
        -- so reset status before command executed.
        listing_shown = false
        local success, done, hook = pcall(run_command,
                                         dbg.read(color_red(DEBUGGER .. "> ")))
        if success then
            debug.sethook(hook and hook(0), "l")
        else
            local message = color_red("INTERNAL " .. DEBUGGER .. " ERROR. " ..
                            "ABORTING\n:") .. " " .. done
            dbg_writeln(message)
            error(message)
        end
    until done
end

-- Make the debugger object callable like a function.
dbg = setmetatable({
        read    = dbg_readline,
        write   = dbg_write,
        writeln = dbg_writeln,

        shorten_path = function(path) return path end,
        exit    = function(err) os.exit(err) end,

        cfg = {
            auto_where  = 3,
            auto_eval   = false,
            pretty_depth = 3,
        },
        pretty  = pretty,
        pp = function(value, depth)
            dbg_writeln(pretty(value, depth))
        end,
    }, {
    __call = function(_, condition, top_offset, source)
        if condition then
            return
        end

        top_offset = top_offset or 0
        stack_inspect_offset = top_offset
        stack_top = top_offset

        local hook_next = hook_factory(current_stack_level())
        debug.sethook(hook_next(source or "dbg()"), "l")
        return
    end,
})

local lua_error, lua_assert = error, assert

-- Works like error(), but invokes the debugger.
function dbg.error(err, level)
    level = level or 1
    dbg_write_error(pretty(err))
    dbg(false, level, "dbg.error()")

    lua_error(err, level)
end

-- Works like assert(), but invokes the debugger on a failure.
function dbg.assert(condition, message)
    if not condition then
        dbg_write_error(message)
        dbg(false, 1, "dbg.assert()")
    end

    return lua_assert(condition, message)
end

-- Works like pcall(), but invokes the debugger on an error.
function dbg.call(f, ...)
    return xpcall(f, function(err)
        dbg_write_error(pretty(err))
        dbg(false, 1, "dbg.call()")

        return err
    end, ...)
end

-- Start an interactive debugging console.
function dbg.start(path, ...)
    assert(path ~= nil and type(path) == 'string')
    assert(fio.stat(path) ~= nil)

    stack_inspect_offset = 0
    stack_top = 0
    local hook_step = hook_factory(math.huge)
    debug.sethook(hook_step(path), "l")
    dofile(path, ...);
end

-- Error message handler that can be used with lua_pcall().
function dbg.msgh(...)
    if debug.getinfo(2) then
        dbg_write_error(pretty(...))
        dbg(false, 1, "dbg.msgh()")
    else
        dbg_write_error(DEBUGGER .. ": " ..
                    "Error did not occur in Lua code. " ..
                    "Execution will continue after dbg_pcall().")
    end

    return ...
end

local ffi = require("ffi")
ffi.cdef [[ int isatty(int); ]]

local stdout_isatty = ffi.C.isatty(1)

-- Conditionally enable color support.
local color_maybe_supported = (stdout_isatty and os.getenv("TERM") and os.getenv("TERM") ~= "dumb")
if color_maybe_supported and not os.getenv("NO_COLOR") then
    COLOR_GRAY = string.char(27) .. "[90m"
    COLOR_RED = string.char(27) .. "[91m"
    COLOR_GREEN = string.char(27) .. "[92m"
    COLOR_BLUE = string.char(27) .. "[94m"
    COLOR_YELLOW = string.char(27) .. "[33m"
    COLOR_RESET = string.char(27) .. "[0m"

    CARET_SYM = COLOR_GREEN .. "=>" .. COLOR_RESET
    CARET = " " .. CARET_SYM .. " "
    BREAK_SYM = COLOR_RED .. "●" .. COLOR_RESET
end

return dbg
