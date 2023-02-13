local fun = require('fun')
local log = require('log')
local json = require('json')
local popen = require('popen')

local justrun = {}

-- Run tarantool in given directory with given environment and
-- command line arguments and catch its output.
--
-- Expects JSON lines as the output and parses it into an array.
function justrun.tarantool(dir, env, args)
    assert(type(dir) == 'string')
    assert(type(env) == 'table')
    assert(type(args) == 'table')

    local tarantool_exe = arg[-1]
    -- Use popen.shell() instead of popen.new() due to lack of
    -- cwd option in popen (gh-5633).
    local env_str = table.concat(fun.iter(env):map(function(k, v)
        return ('%s=%q'):format(k, v)
    end):totable(), ' ')
    local command = ('cd %s && %s %s %s'):format(dir, env_str, tarantool_exe,
                                                 table.concat(args, ' '))
    log.info(('Running a command: %s'):format(command))
    local ph = popen.shell(command, 'r')

    -- Read everything until EOF.
    local chunks = {}
    while true do
        local chunk, err = ph:read()
        if chunk == nil then
            ph:close()
            error(err)
        end
        if chunk == '' then -- EOF
            break
        end
        table.insert(chunks, chunk)
    end

    local exit_code = ph:wait().exit_code
    ph:close()

    -- If an error occurs, discard the output and return only the
    -- exit code.
    if exit_code ~= 0 then
        return {exit_code = exit_code}
    end

    -- Glue all chunks, strip trailing newline.
    local res = table.concat(chunks):rstrip()
    log.info(('Command output:\n%s'):format(res))

    -- Decode JSON object per line into array of tables.
    local decoded = fun.iter(res:split('\n')):map(json.decode):totable()
    return {
        exit_code = exit_code,
        stdout = decoded,
    }
end

return justrun
