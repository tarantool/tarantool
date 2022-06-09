#!/usr/bin/env tarantool

local fun = require('fun')
local json = require('json')

local TIMEOUT_INFINITY = 500 * 365 * 86400

local function default_cfg()
    return {
        work_dir = os.getenv('TARANTOOL_WORKDIR'),
        listen = os.getenv('TARANTOOL_LISTEN'),
        log = ('%s/%s.log'):format(os.getenv('TARANTOOL_WORKDIR'),
            os.getenv('TARANTOOL_ALIAS')),
    }
end

local function env_cfg()
    local cfg = os.getenv('TARANTOOL_BOX_CFG')
    if cfg == nil then
        return {}
    end

    local res = json.decode(cfg)
    assert(type(res) == 'table')
    return res
end

-- Create a table for box.cfg from values passed while server initialization and
-- the given argument.
local function box_cfg(cfg)
    return fun.chain(default_cfg(), env_cfg(), cfg or {}):tomap()
end

-- Set the shutdown timeout to infinity so that we can catch tests that leave
-- asynchronous requests. If we used the default timeout of 3 seconds, such a
-- test would still pass, but it would slow down the overall test run, because
-- the server would take longer to stop. Setting the timeout to infinity makes
-- such bad tests hang and fail.
box.ctl.set_on_shutdown_timeout(TIMEOUT_INFINITY)

box.cfg(box_cfg())
box.schema.user.grant('guest', 'super', nil, nil, {if_not_exists = true})

-- The Server:start function unblocks only when this variable becomes true.
--
-- Set it when the instance is fully operable:
--   * The server listens for requests.
--   * The database is bootstrapped.
--   * Permissions are granted.
--
-- Use server:start({wait_for_readiness = false}) to not wait for setting this
-- variable.
_G.ready = true
