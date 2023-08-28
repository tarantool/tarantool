-- A set of helpers to ease testing of tarantool's interactive
-- mode.

local fun = require('fun')
local fiber = require('fiber')
local log = require('log')
local yaml = require('yaml')
local popen = require('popen')
local tnt = require('tarantool')
local t = require('luatest')

-- Default timeout for expecting an input on child's stdout.
--
-- Testing code shouldn't just hang, it rather should raise a
-- meaningful error for debugging.
local TIMEOUT = 5

local M = {}

local mt = {}
mt.__index = mt

local dbg_header = tnt.package .. " debugger " .. tnt.version

-- {{{ Instance methods

function mt._start_stderr_logger(self)
    local f = fiber.create(function()
        local fiber_name = "child's stderr logger"
        fiber.name(fiber_name, {truncate = true})

        while true do
            local chunk, err = self.ph:read({stderr = true})
            if chunk == nil then
                log.warn(('%s: got error, exitting: %s'):format(
                    fiber_name, tostring(err)))
                break
            end
            if chunk == '' then
                log.info(('%s: got EOF, exitting'):format(fiber_name))
                break
            end
            for _, line in ipairs(chunk:rstrip('\n'):split('\n')) do
                log.warn(('%s: %s'):format(fiber_name, line))
            end
        end
    end)
    self._stderr_logger = f
end

function mt._stop_stderr_logger(self)
    if self._stderr_logger == nil then
        return
    end
    -- The server could have finished already, thus
    -- kill only what needs to be killed.
    if self._stderr_logger:status() ~= 'dead' then
        self._stderr_logger:cancel()
    end
    self._stderr_logger = nil
end

function mt.read_chunk(self, opts)
    local opts = opts or {}
    local deadline = opts.deadline or (fiber.clock() + TIMEOUT)

    local chunk, err = self.ph:read({timeout = TIMEOUT})
    if chunk == nil then
        error(err)
    end
    if chunk == '' then
        error('Unexpected EOF')
    end
    if fiber.clock() > deadline then
        error('Timed out')
    end
    self._readahead_buffer = self._readahead_buffer .. chunk
    log.info(("child's stdout logger: %s"):format(M.escape_control(chunk)))
end

function mt.read_line(self, opts)
    local opts = opts or {}
    local deadline = opts.deadline or (fiber.clock() + TIMEOUT)

    while not self._readahead_buffer:find('\n') do
        self:read_chunk({deadline = deadline})
    end
    local line, new_buffer = unpack(self._readahead_buffer:split('\n', 1))
    self._readahead_buffer = new_buffer
    return line
end

-- Returns all read lines (including expected one).
function mt.read_until_line(self, exp_line, opts)
    local opts = opts or {}
    local deadline = opts.deadline or (fiber.clock() + TIMEOUT)

    local res = {}

    repeat
        local line = self:read_line({deadline = deadline})
        table.insert(res, line)
    until line == exp_line

    return res
end

-- Returns all read lines (excluding one with expected prompt, with deadline).
-- Checks if stderr is empty.
function mt.read_until_prompt(self, opts)
    local opts = opts or {}
    local deadline = opts.deadline or (fiber.clock() + TIMEOUT)

    while not self._readahead_buffer:find(self._prompt) do
        self:read_chunk({deadline = deadline})
    end

    local res, new_buffer = unpack(self._readahead_buffer:split(self._prompt,
                                                                1))
    new_buffer = self._prompt .. new_buffer

    self._readahead_buffer = new_buffer

    local stderr, err = self.ph:read({timeout = 0.05, stderr = true})
    if stderr ~= "" and not (stderr == nil and
                             tostring(err) == "timed out") then
        error(("Unexpected stderr output: %s"):format(stderr))
    end

    return res
end

function mt.assert_line(self, exp_line, opts)
    local opts = opts or {}
    local deadline = opts.deadline or (fiber.clock() + TIMEOUT)

    local line = self:read_line({deadline = deadline})
    if line ~= exp_line then
        error(('Unexpected line %q, expected %q'):format(line, exp_line))
    end
end

function mt.assert_data(self, exp_data, opts)
    local opts = opts or {}
    local deadline = opts.deadline or (fiber.clock() + TIMEOUT)

    while #self._readahead_buffer < #exp_data do
        self:read_chunk({deadline = deadline})
    end
    local data = self._readahead_buffer:sub(1, #exp_data)
    local new_buffer = self._readahead_buffer:sub(#exp_data + 1)
    self._readahead_buffer = new_buffer
    if data ~= exp_data then
        error(('Unexpected data %q, expected %q'):format(data, exp_data))
    end
end

local function decolor(string)
    assert(type(string) == 'string')
    -- The gsub is meant to clean ANSI color codes, for more details on
    -- the format see the doc:
    -- https://www.xfree86.org/current/ctlseqs.html
    return string:gsub(M.ESC .. '%[[0-9;]*m', '')
end

-- ReadLine echoes commands to stdout. It is easier to match the
-- echo against the original command, when the command is a
-- one-liner. Let's replace newlines with spaces.
--
-- Add a line feed at the end to send the command for execution.
local function _prepare_command_for_write(command)
    return command:rstrip('\n'):gsub('\n', ' ') .. '\n'
end

-- ReadLine determines terminal's ability to wrap long lines and
-- may perform the wrapping on its own. It adds carriage return,
-- spaces and may duplicate some characters.
--
-- Observed behavior is different on different readline version:
--
-- * ReadLine 8: lean on terminal's wrapping.
-- * ReadLine 7: x<CR><duplicate x>, extra spaces.
-- * ReadLine 6: <space><CR>.
--
-- This function drop duplicates that goes in row, strips <CR>,
-- spaces, tabs and ANSI color codes. Applying of this function
-- to a source command and readline's echoed command allows to
-- compare them for equality.
local function _prepare_command_for_compare(x)
    x = x:gsub(' ', '')
         :gsub(M.CR, '')
         :gsub(M.TAB, '')

    x = decolor(x)
    local acc = fun.iter(x):reduce(function(acc, c)
        if acc[#acc] ~= c then
            table.insert(acc, c)
        end
        return acc
    end, {})
    return table.concat(acc)
end

function mt._assert_command_echo(self, prepared_command, opts)
    local opts = opts or {}
    local deadline = opts.deadline or (fiber.clock() + TIMEOUT)

    local exp_echo = self._prompt .. prepared_command:rstrip('\n')
    local echo = self:read_line({deadline = deadline})

    -- If readline wraps the line, prepare the commands for
    -- compare.
    exp_echo = _prepare_command_for_compare(exp_echo)
    echo = _prepare_command_for_compare(echo)
    local comment = ' (the commands are mangled for the comparison)'

    if echo ~= exp_echo then
        error(('Unexpected command echo %q, expected %q%s'):format(
            echo, exp_echo, comment))
    end
end

function mt.execute_command(self, command, opts)
    local opts = opts or {}
    local deadline = opts.deadline or (fiber.clock() + TIMEOUT)

    local prepared_command = _prepare_command_for_write(command)
    self.ph:write(prepared_command)
    self:_assert_command_echo(prepared_command, {deadline = deadline})
end

-- Ignores output before yaml start document marks, because
-- print() output may appear before it.
function mt.read_response(self, opts)
    local opts = opts or {}
    local deadline = opts.deadline or (fiber.clock() + TIMEOUT)

    self:read_until_line('---', {deadline = deadline})
    local lines = self:read_until_line('...', {deadline = deadline})
    self:assert_line('', {deadline = deadline})

    -- Handle empty response.
    if #lines == 1 then
        return
    end

    local raw_reply = '---\n' .. table.concat(lines, '\n') .. '\n'
    local reply = yaml.decode(raw_reply)

    -- Consider reply an error if the following conditions are met:
    --
    -- 1. The reply contains just one response.
    -- 2. The response is a table.
    -- 3. The table contains only one key.
    -- 4. This key is 'error'.
    --
    -- This is how tarantool's console serializes a raised error.
    local is_error = #reply == 1 and type(reply[1]) == 'table' and
        next(reply[1], next(reply[1])) == nil and reply[1].error ~= nil
    if is_error then
        error(reply[1].error, 0)
    end

    return unpack(reply, 1, table.maxn(reply))
end

function mt.assert_empty_response(self, opts)
    local reply = {self:read_response(opts)}
    if table.maxn(reply) ~= 0 then
        error(('Unexpected non-empty response:\n%s'):format(yaml.encode(reply)))
    end
end

-- Prompt may be different for remote console.
function mt.set_prompt(self, prompt)
    self._prompt = prompt
end

function mt.prompt(self)
    return self._prompt
end

function mt.close(self)
    self:_stop_stderr_logger()
    self.ph:close()
end

-- Run a command and assert response.
function mt.roundtrip(self, command, expected)
    self:execute_command(command)
    local response = self:read_response()
    if expected ~= nil then
        t.assert_equals(response, expected)
    end
end

-- }}} Instance methods

-- {{{ Module functions

function M.escape_control(str)
    return str
        :gsub(M.TAB, '<TAB>')
        :gsub(M.LF, '<LF>')
        :gsub(M.CR, '<CR>')
        :gsub(M.ESC, '<ESC>')
end

function M._new_internal(opts)
    local opts = opts or {}
    local args = opts.args or {}
    local env = opts.env or {}
    local prompt = opts.prompt

    local tarantool_exe = arg[-1]
    local ph = popen.new(fun.chain({tarantool_exe, '-i'}, args):totable(), {
        stdin = popen.opts.PIPE,
        stdout = popen.opts.PIPE,
        stderr = popen.opts.PIPE,
        env = fun.chain({
            -- Don't know why, but without defined TERM environment
            -- variable readline doesn't accept INPUTRC environment
            -- variable.
            TERM = 'xterm',
            -- Prevent system/user inputrc configuration file from
            -- influence testing code. In particular, if
            -- horizontal-scroll-mode is enabled,
            -- _assert_command_echo() on a long command will fail
            -- (because the command in the echo output will be
            -- trimmed).
            INPUTRC = '/dev/null',
        }, env):tomap(),
    })

    local res = setmetatable({
        ph = ph,
        _readahead_buffer = '',
        _prompt = prompt or 'tarantool> ',
    }, mt)

    return res
end

function M.new_debugger(opts)
    opts = opts or {}
    opts.prompt = opts.prompt or 'luadebug> '
    local debugger = M._new_internal(opts)
    if opts.expect_header then
        local stderr, err = debugger.ph:read({timeout = TIMEOUT, stderr = true})
        if (stderr == nil) then
            error(err)
        end
        assert(stderr:find(dbg_header, 0, true))
    end
    return debugger
end

function M.new(opts)
    local res = M._new_internal(opts)

    -- Write a command and ignore the echoed output.
    --
    -- ReadLine 6 writes <ESC>[?1034h at beginning, it may hit
    -- assertions on the child's output.
    --
    -- This sequence of characters is smm ('set meta mode')
    -- terminal capacity value. It has relation to writing
    -- characters out of the ASCII range -- ones with 8th bit set,
    -- but its description is vague. See terminfo(5).
    res.ph:write("'Hello!'\n")
    res:read_line()
    assert(res:read_response() == 'Hello!')

    -- Disable stdout line buffering in the child.
    res:execute_command("io.stdout:setvbuf('no')")
    assert(res:read_response(), true)

    -- Log child's stderr.
    res:_start_stderr_logger()

    return res
end

-- }}} Module functions

-- {{{ Module constants

M.TAB = '\x09'
M.LF = '\x0a'
M.CR = '\x0d'
M.ESC = '\x1b'

M.ERASE_IN_LINE = M.ESC .. '[K'

-- }}} Module constants

return M
