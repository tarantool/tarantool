#!/usr/bin/env tarantool
local helpers = require('test.luatest_helpers')

box.cfg(helpers.box_cfg())
box.schema.user.grant('guest', 'super', nil, nil, {if_not_exists = true})

-- luatest_helpers.Server:start() unblocks only when this variable
-- becomes true.
--
-- Set it when the instance is fully operable:
--
-- * The server listens for requests.
-- * The database is bootstrapped.
-- * Permissions are granted.
--
-- Use luatest_helpers.Server:start({wait_for_readiness = false})
-- to don't wait for setting of this variable.
_G.ready = true
