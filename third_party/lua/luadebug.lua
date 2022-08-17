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

local DEBUGGER = 'luadebug.lua'
-- Use ANSI color codes in the prompt by default.
local COLOR_GRAY = ""
local COLOR_RED = ""
local COLOR_BLUE = ""
local COLOR_YELLOW = ""
local COLOR_RESET = ""
local GREEN_CARET = " => "
local auto_listing = true

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

-- The stack level that cmd_* functions use to access locals or info
-- The structure of the code very carefully ensures this.
local CMD_STACK_LEVEL = 6

-- Location of the top of the stack outside of the debugger.
-- Adjusted by some debugger entrypoints.
local stack_top = 0

-- The current stack frame index.
-- Changed using the up/down commands
local stack_inspect_offset = 0

-- Default dbg.read function
local function dbg_read(prompt)
    dbg.write(prompt)
    io.flush()
    return io.read()
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

local repl

-- Return false for stack frames without source,
-- which includes C frames, Lua bytecode, and `loadstring` functions
local function frame_has_line(info) return info.currentline >= 0 end

local function hook_factory(repl_threshold)
    return function(offset, reason)
        return function(event, _)
            -- Skip events that don't have line information.
            if not frame_has_line(debug.getinfo(2)) then return end

            -- Tail calls are specifically ignored since they also will have tail returns to balance out.
            if event == "call" then
                offset = offset + 1
            elseif event == "return" and offset > repl_threshold then
                offset = offset - 1
            elseif event == "line" and offset <= repl_threshold then
                repl(reason)
            end
        end
    end
end

local hook_step = hook_factory(1)
local hook_next = hook_factory(0)
local hook_finish = hook_factory(-1)

-- Create a table of all the locally accessible variables.
-- Globals are not included when running the locals command, but are when running the print command.
local function local_bindings(offset, include_globals)
    local level = offset + stack_inspect_offset + CMD_STACK_LEVEL
    local func = debug.getinfo(level).func
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
                dbg_writeln(color_yellow(DEBUGGER) .. GREEN_CARET ..
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
        dbg_writeln(COLOR_RED .. "Error: Could not compile block:\n" ..
            COLOR_RESET .. block)
    end
    return chunk
end

local SOURCE_CACHE = {}

local function where(info, context_lines)
    local filesource = info.source
    local source = SOURCE_CACHE[info.source]
    if not source then
        source = {}
        -- Tarantool builtin module
        if filesource:match("@builtin/.*.lua") then
            pcall(function()
                local tnt_debug = require('tarantool').debug
                local lua_code = tnt_debug.getsources(filesource)

                for line in string.gmatch(lua_code, "([^\n]*)\n?") do
                    table.insert(source, line)
                end
            end)
        else
            -- external module - load file
            local filename = filesource:match("@(.*)")
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

    if source and source[info.currentline] then
        for i = info.currentline - context_lines, info.currentline + context_lines do
            local tab_or_caret = (i == info.currentline and GREEN_CARET or "    ")
            local line = source[i]
            if line then
                dbg_writeln(color_grey("% 4d") .. tab_or_caret .. "%s", i, line)
            end
        end
    else
        dbg_writeln(color_red("Error: Source not available for ") ..
                    color_blue(q(info.source)))
    end

    return false
end

-- Wee version differences
local unpack = unpack or table.unpack
local pack = function(...) return { n = select("#", ...), ... } end

local function cmd_step()
    stack_inspect_offset = stack_top
    return true, hook_step
end

local function cmd_next()
    stack_inspect_offset = stack_top
    return true, hook_next
end

local function cmd_finish()
    local offset = stack_top - stack_inspect_offset
    stack_inspect_offset = stack_top
    return true, offset < 0 and hook_factory(offset - 1) or hook_finish
end

local function cmd_print(expr)
    local env = local_bindings(1, true)
    local chunk = compile_chunk("return " .. expr, env)
    if chunk == nil then return false end

    -- Call the chunk and collect the results.
    local results = pack(pcall(chunk, unpack(rawget(env, "...") or {})))

    -- The first result is the pcall error.
    if not results[1] then
        dbg_writeln(color_red("Error:") .. " " .. results[2])
    else
        local output = ""
        for i = 2, results.n do
            output = output .. (i ~= 2 and ", " or "") .. pretty(results[i])
        end

        if output == "" then output = "<no result>" end
        dbg_writeln(color_blue(expr) .. GREEN_CARET .. output)
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
        dbg_writeln(color_red("Error:") .. " " .. tostring(err))
    end

    return false
end

local function cmd_down()
    local offset = stack_inspect_offset
    local info

    repeat -- Find the next frame with a file.
        offset = offset + 1
        info = debug.getinfo(offset + CMD_STACK_LEVEL)
    until not info or frame_has_line(info)

    if info then
        stack_inspect_offset = offset
        dbg_writeln("Inspecting frame: " .. format_stack_frame_info(info))
        if tonumber(dbg.cfg.auto_where) then
            where(info, dbg.cfg.auto_where)
        end
    else
        dbg_writeln("Already at the bottom of the stack.")
    end

    return false
end

local function cmd_up()
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
        if tonumber(dbg.cfg.auto_where) then
            where(info, dbg.cfg.auto_where)
        end
    else
        dbg_writeln("Already at the top of the stack.")
    end

    return false
end

local function cmd_where(context_lines)
    local info = debug.getinfo(stack_inspect_offset + CMD_STACK_LEVEL)
    return (info and where(info, tonumber(context_lines) or 5))
end

local function cmd_listing(context_lines)
    local offset = stack_inspect_offset + CMD_STACK_LEVEL - 2
    local info = debug.getinfo(offset)
    return (info and where(info, tonumber(context_lines) or 5))
end

local function cmd_trace()
    dbg_writeln("Inspecting frame %d", stack_inspect_offset - stack_top)
    local i = 0;
    while true do
        local info = debug.getinfo(stack_top + CMD_STACK_LEVEL + i)
        if not info then break end

        local is_current_frame = (i + stack_top == stack_inspect_offset)
        local tab_or_caret = (is_current_frame and GREEN_CARET or "    ")
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
            dbg_writeln("  " .. color_blue(k) .. GREEN_CARET .. pretty(v))
        end
    end

    return false
end

local function cmd_help()
    dbg.write(""
        .. COLOR_BLUE .. "  <return>" .. GREEN_CARET .. "re-run last command\n"
        .. COLOR_BLUE .. "  c" .. COLOR_YELLOW .. "(ontinue)" .. GREEN_CARET .. "continue execution\n"
        .. COLOR_BLUE .. "  s" .. COLOR_YELLOW .. "(tep)" .. GREEN_CARET ..
        "step forward by one line (into functions)\n"
        ..
        COLOR_BLUE .. "  n" ..
        COLOR_YELLOW .. "(ext)" .. GREEN_CARET .. "step forward by one line (skipping over functions)\n"
        .. COLOR_BLUE ..
        "  f" .. COLOR_YELLOW .. "(inish)" .. GREEN_CARET .. "step forward until exiting the current function\n"
        .. COLOR_BLUE .. "  u" .. COLOR_YELLOW .. "(p)" .. GREEN_CARET .. "move up the stack by one frame\n"
        .. COLOR_BLUE .. "  d" .. COLOR_YELLOW .. "(own)" .. GREEN_CARET .. "move down the stack by one frame\n"
        ..
        COLOR_BLUE ..
        "  w" ..
        COLOR_YELLOW ..
        "(here) " .. COLOR_BLUE .. "[line count]" .. GREEN_CARET .. "print source code around the current line\n"
        .. COLOR_BLUE ..
        "  e" .. COLOR_YELLOW .. "(val) " .. COLOR_BLUE .. "[statement]" .. GREEN_CARET .. "execute the statement\n"
        ..
        COLOR_BLUE ..
        "  p" ..
        COLOR_YELLOW ..
        "(rint) " .. COLOR_BLUE .. "[expression]" .. GREEN_CARET .. "execute the expression and print the result\n"
        .. COLOR_BLUE .. "  t" .. COLOR_YELLOW .. "(race)" .. GREEN_CARET .. "print the stack trace\n"
        ..
        COLOR_BLUE ..
        "  l" .. COLOR_YELLOW .. "(ocals)" .. GREEN_CARET .. "print the function arguments, locals and upvalues.\n"
        .. COLOR_BLUE .. "  h" .. COLOR_YELLOW .. "(elp)" .. GREEN_CARET .. "print this message\n"
        .. COLOR_BLUE .. "  q" .. COLOR_YELLOW .. "(uit)" .. GREEN_CARET .. "halt execution\n"
    )
    return false
end

local last_cmd = false

local commands = {
    ["^c$"] = function() return true end,
    ["^s$"] = cmd_step,
    ["^n$"] = cmd_next,
    ["^f$"] = cmd_finish,
    ["^p%s+(.*)$"] = cmd_print,
    ["^e%s+(.*)$"] = cmd_eval,
    ["^u$"] = cmd_up,
    ["^d$"] = cmd_down,
    ["^w%s*(%d*)$"] = cmd_where,
    ["^t$"] = cmd_trace,
    ["^l$"] = cmd_locals,
    ["^h$"] = cmd_help,
    ["^q$"] = function() dbg.exit(0); return true end,
}

local function match_command(line)
    for pat, func in pairs(commands) do
        -- Return the matching command and capture argument.
        if line:find(pat) then return func, line:match(pat) end
    end
end

-- Run a command line
-- Returns true if the REPL should exit and the hook function factory
local function run_command(line)
    -- GDB/LLDB exit on ctrl-d
    if line == nil then dbg.exit(1); return true end

    -- Re-execute the last command if you press return.
    if line == "" then line = last_cmd or "h" end

    local command, command_arg = match_command(line)
    if command then
        last_cmd = line
        -- unpack({...}) prevents tail call elimination so the stack frame indices are predictable.
        return unpack({ command(command_arg) })
    elseif dbg.cfg.auto_eval then
        return unpack({ cmd_eval(line) })
    else
        dbg_writeln(color_red("Error:") ..
            " command '%s' not recognized.\n" ..
            "Type 'h' and press return for a command list.", line)
        return false
    end
end

repl = function(reason)
    -- Skip frames without source info.
    while not frame_has_line(debug.getinfo(stack_inspect_offset + CMD_STACK_LEVEL - 3)) do
        stack_inspect_offset = stack_inspect_offset + 1
    end

    local info = debug.getinfo(stack_inspect_offset + CMD_STACK_LEVEL - 3)
    reason = reason and (color_yellow("break via ") .. color_red(reason) ..
             GREEN_CARET) or ""
    dbg_writeln(reason .. format_stack_frame_info(info))

    if tonumber(dbg.cfg.auto_where) then
        where(info, dbg.cfg.auto_where)
    end

    repeat
        if auto_listing then
            pcall(cmd_listing(3))
        end
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
        read    = dbg_read,
        write   = dbg_write,
        writeln = dbg_writeln,

        shorten_path = function(path) return path end,
        exit    = function(err) os.exit(err) end,

        cfg = {
            auto_where  = false,
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

        debug.sethook(hook_next(1, source or "dbg()"), "crl")
        return
    end,
})

local lua_error, lua_assert = error, assert

-- Works like error(), but invokes the debugger.
function dbg.error(err, level)
    level = level or 1
    dbg_writeln(color_red("ERROR: ") .. pretty(err))
    dbg(false, level, "dbg.error()")

    lua_error(err, level)
end

-- Works like assert(), but invokes the debugger on a failure.
function dbg.assert(condition, message)
    if not condition then
        dbg_writeln(color_red("ERROR:") .. message)
        dbg(false, 1, "dbg.assert()")
    end

    return lua_assert(condition, message)
end

-- Works like pcall(), but invokes the debugger on an error.
function dbg.call(f, ...)
    return xpcall(f, function(err)
        dbg_writeln(color_red("ERROR: ") .. pretty(err))
        dbg(false, 1, "dbg.call()")

        return err
    end, ...)
end

-- Error message handler that can be used with lua_pcall().
function dbg.msgh(...)
    if debug.getinfo(2) then
        dbg_writeln(color_red("ERROR: ") .. pretty(...))
        dbg(false, 1, "dbg.msgh()")
    else
        dbg_writeln(color_red(DEBUGGER .. ": ") ..
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
if color_maybe_supported and not os.getenv("DBG_NOCOLOR") then
    COLOR_GRAY = string.char(27) .. "[90m"
    COLOR_RED = string.char(27) .. "[91m"
    COLOR_BLUE = string.char(27) .. "[94m"
    COLOR_YELLOW = string.char(27) .. "[33m"
    COLOR_RESET = string.char(27) .. "[0m"
    GREEN_CARET = string.char(27) .. "[92m => " .. COLOR_RESET
end

-- Detect Lua/LuaJIT version.
dbg_writeln(color_yellow(DEBUGGER .. ": ") .. "Loaded for " .. jit.version)

return dbg
